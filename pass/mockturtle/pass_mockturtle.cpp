//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_mockturtle.hpp"

void setup_pass_mockturtle() {
  Pass_mockturtle p;
  p.setup();
}

void Pass_mockturtle::setup() {
  Eprp_method m1("pass.mockturtle", "pass a lgraph using mockturtle", &Pass_mockturtle::work);

  register_pass(m1);
}

Pass_mockturtle::Pass_mockturtle()
    : Pass("mockturtle") {
}

void Pass_mockturtle::work(Eprp_var &var) {
  Pass_mockturtle pass;

  for(const auto &g : var.lgs) {
    pass.do_work(g);
  }
}

void Pass_mockturtle::lg_partition(LGraph *g) {
  std::map<int, int> group_id_mapping;
  int group_id = 0;

  for(const auto &nid : g->forward()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished

    fmt::print("Node identifier:{}\n", node.get_compact());
    if (eligable_cell_op(node.get_type().op)) {

      if (group_boundary.find(node.get_compact())==group_boundary.end()) {

        int current_node_group_id = 0;

        for(const auto &in_edge : node.inp_edges()) {
          auto peer_driver_node = in_edge.driver.get_node();
          if (group_boundary.find(peer_driver_node.get_compact())!=group_boundary.end()) {
            current_node_group_id = group_boundary[peer_driver_node.get_compact()];
            break;
          }
        }

        if (current_node_group_id == 0) {
          group_id++;
          current_node_group_id = group_id;
        }
        group_boundary[node.get_compact()] = current_node_group_id;
      }

      for(const auto &out_edge : node.out_edges()) {
        auto peer_sink_node = out_edge.sink.get_node();
        if (eligable_cell_op(peer_sink_node.get_type().op)) {
          if (group_boundary.find(peer_sink_node.get_compact())==group_boundary.end()) {
            group_boundary[peer_sink_node.get_compact()] = group_boundary[node.get_compact()];
          }
          else {
            group_id_mapping[group_boundary[node.get_compact()]]=group_boundary[peer_sink_node.get_compact()];
          }
        }
      }
    }
  }

  for (std::map<int,int>::reverse_iterator rit = group_id_mapping.rbegin(); rit!=group_id_mapping.rend(); rit++) {
    for (auto &it:group_boundary) {
      if (it.second==rit->first)
        it.second=rit->second;
    }
  }

  for (const auto &it:group_boundary) {
    group_boundary_set[it.second].emplace_back(it.first);
  }
}

void Pass_mockturtle::setup_input_signal(const XEdge &input_edge, std::vector<mockturtle::mig_network::signal> &input_signal, mockturtle::mig_network &mig) {
  //check if this input edge is already in the output mapping table
  //then setup the input signal accordingly
  if (edge2signal.count(input_edge)!=0) {
    //fetch the input signal from mapping table
    for (auto i=0;i<input_edge.get_bits();i++) {
      input_signal.emplace_back(edge2signal[input_edge][i]);
    }
  } else {
    //create new input signals and map them back
    for (auto i=0;i<input_edge.get_bits();i++) {
      input_signal.emplace_back(mig.create_pi());
    }
    edge2signal[input_edge]=input_signal;
  }
}

void Pass_mockturtle::setup_output_signal(const XEdge &output_edge, std::vector<mockturtle::mig_network::signal> &output_signal, mockturtle::mig_network &mig) {
  //check if the output edge is already in the input mapping table
  //then setup/update the output/input table
  if (edge2signal.count(output_edge)!=0) {
    for (auto i=0;i<output_edge.get_bits();i++) {
      mockturtle::mig_network::node old_node = mig.get_node(edge2signal[output_edge][i]);
      mig.substitute_node(old_node,output_signal[i]);
      mig.take_out_node(old_node);
      //FIX ME: remove dangling input node when converting to lgraph
      //because take_out_node() does not remove primary input nodes.
    }
  }
  //mapping output edge to output signal
  edge2signal[output_edge]=output_signal;
}

//split the input signal by bits
void Pass_mockturtle::split_input_signal(const std::vector<mockturtle::mig_network::signal> &input_signal, std::vector<std::vector<mockturtle::mig_network::signal>> &splitted_input_signal) {
  for (long unsigned int i=0;i<input_signal.size();i++) {
    if (splitted_input_signal.size()<=i) {
      splitted_input_signal.resize(i+1);
    }
    splitted_input_signal[i].emplace_back(input_signal[i]);
  }
}

void Pass_mockturtle::create_network_output_signal(LGraph *g, int gid, mockturtle::mig_network &mig) {
  I(group_boundary_set.count(gid)!=0);
  absl::flat_hash_set<Node::Compact> node_set;
  for (const auto node_compact:group_boundary_set[gid]) {
    node_set.insert(node_compact);
  }
  for (const auto node_compact:node_set) {
    auto node = Node(g, 0, node_compact);
    for (const auto &out_edge : node.out_edges()) {
      if (node_set.find((out_edge.sink.get_node().get_compact()))==node_set.end()) {
        for (const auto sig : edge2signal[out_edge]) {
          mig.create_po(sig);
        }
      }
    }
  }
}

void Pass_mockturtle::create_LUT_network(LGraph *g) {
  for (const auto gid:group_boundary_set) {
    auto mig = mockturtle::mig_network();
    //traverse the nodes in lgraph and create nodes/signals in mig network
    for (const auto gid_node:gid.second) {
      auto cur_node = Node(g, 0, gid_node);
      switch (cur_node.get_type().op) {
        case Not_Op: {
          //Note: Don't need to check the node_pin pid since Not_Op has only one sink pin and one driver pin
          fmt::print("Not_Op in gid:{}\n",gid.first);
          I(cur_node.inp_edges().size()==1 && cur_node.out_edges().size()>0);

          std::vector<mockturtle::mig_network::signal> inp_sig, out_sig;
          unsigned long int loop_count = 0;
          //processing input signal
          for (const auto &in_edge : cur_node.inp_edges()) {
            fmt::print("input_bit_width:{}\n",in_edge.get_bits());
            setup_input_signal(in_edge, inp_sig, mig);
            loop_count++;
          }
          //this loop should be executed only once
          //since a Not_Op should only have single input edge
          I(loop_count==1);
          //creating output signal
          for (long unsigned int i=0;i<inp_sig.size();i++) {
            out_sig.emplace_back(mig.create_not(inp_sig[i]));
          }
          //processing output signal
          for (const auto &out_edge : cur_node.out_edges()) {
            fmt::print("output_bit_width:{}\n",out_edge.get_bits());
            //make sure the bit-width matches each other
            I(out_edge.get_bits()==out_sig.size());
            setup_output_signal(out_edge, out_sig, mig);
          }
          break;
        }

        case And_Op: {
          fmt::print("And_Op in gid:{}\n",gid.first);
          I(cur_node.inp_edges().size()>0 && cur_node.out_edges().size()>0);

          //mapping all input edge to input signal
          //out_sig_0: regular AND
          //out_sig_1: reduced AND
          std::vector<mockturtle::mig_network::signal> out_sig_0, out_sig_1;
          std::vector<std::vector<mockturtle::mig_network::signal>> inp_sig_group_by_bit;
          //processing input signal
          for (const auto &in_edge : cur_node.inp_edges()) {
            fmt::print("input_bit_width:{}\n",in_edge.get_bits());
            std::vector<mockturtle::mig_network::signal> inp_sig;
            setup_input_signal(in_edge, inp_sig, mig);
            split_input_signal(inp_sig, inp_sig_group_by_bit);
          }
          //creating output signal
          switch (cur_node.inp_edges().size()) {
            case 1: {
              for (long unsigned int i = 0; i < inp_sig_group_by_bit.size(); i++) {
                out_sig_0.emplace_back(inp_sig_group_by_bit[i][0]);
              }
              break;
            }
            default: {
              for (long unsigned int i = 0; i < inp_sig_group_by_bit.size(); i++) {
                out_sig_0.emplace_back(mig.create_nary_and(inp_sig_group_by_bit[i]));
              }
              break;
            }
          }
          out_sig_1.emplace_back(mig.create_nary_and(out_sig_0));
          //processing output signal
          for (const auto &out_edge : cur_node.out_edges()) {
            switch (out_edge.driver.get_pid()) {
              case 0: {
                I(out_edge.get_bits()==out_sig_0.size());
                setup_output_signal(out_edge, out_sig_0, mig);
                break;
              }
              case 1: {
                I(out_edge.get_bits()==out_sig_1.size());
                setup_output_signal(out_edge, out_sig_1, mig);
                break;
              }
              default:
                I(false);
                break;
            }
          }
          //FIX ME: delete unused output signal
          break;
        }
/*
        case Or_Op:
          fmt::print("Or_Op in gid:{}\n",gid.first);
          break;
        case Xor_Op:
          fmt::print("Xor_Op in gid:{}\n",gid.first);
          break;
*/
        case Equals_Op: {
          fmt::print("Equals_Op in gid:{}\n",gid.first);
          I(cur_node.inp_edges().size()>1 && cur_node.out_edges().size()>0);
          //mapping all input edge to input signal
          std::vector<mockturtle::mig_network::signal> out_sig, med_sig;
          std::vector<std::vector<mockturtle::mig_network::signal>> inp_sig_group_by_bit;
          for (const auto &in_edge : cur_node.inp_edges()) {
            fmt::print("input_bit_width:{}\n",in_edge.get_bits());
            std::vector<mockturtle::mig_network::signal> inp_sig;
            setup_input_signal(in_edge, inp_sig, mig);
            split_input_signal(inp_sig, inp_sig_group_by_bit);
          }
          //creating output signal
          //A[0..n]==B[0..n]==C[0..n] iff
          //A0^B0==0 && B0^C0==0 && A1^B1==0 && B1^C1==0
          //&& ... && AN^BN==0 && BN^CN==0
          //that is: ~(|{(A0,B0),(B0,C0) ... (AN,BN), (BN,CN)})==1
          for (long unsigned int i = 0; i < inp_sig_group_by_bit.size(); i++) {
            for (long unsigned int j = 1; j < inp_sig_group_by_bit[i].size(); j++) {
              med_sig.emplace_back(mig.create_xor(inp_sig_group_by_bit[i][j-1],inp_sig_group_by_bit[i][j]));
            }
          }
          out_sig.emplace_back(mig.create_not(mig.create_nary_or(med_sig)));
          //processing output signal
          for (const auto &out_edge : cur_node.out_edges()) {
            I(out_edge.get_bits()==1);
            setup_output_signal(out_edge, out_sig, mig);
          }
          break;
        }
/*
        case LessThan_Op: {
          //CAUTION: mockturtle has neither binary nor n-ary LessThan
          //therefore the implementation would be tricky and TBD
          fmt::print("LessThan_Op in gid:{}\n",gid.first);
          I(cur_node.inp_edges().size()>1 && cur_node.out_edges().size()>0);
          //
          break;
        }
*/
        default:
          fmt::print("Unknown_Op in gid:{}\n",gid.first);
          break;
      }

    }
    create_network_output_signal(g, gid.first, mig);
    fmt::print("MIG network under Group ID:{}\n", gid.first);
    mig = cleanup_dangling(mig);
    mockturtle::write_bench(mig,std::cout);
    //converting mig to klut
    mockturtle::mapping_view<mockturtle::mig_network, true> mapped_mig{mig};
    mockturtle::lut_mapping_params ps;
    mockturtle::lut_mapping<mockturtle::mapping_view<mockturtle::mig_network, true>, true>(mapped_mig, ps);
    mockturtle::klut_network klut =*mockturtle::collapse_mapped_network<mockturtle::klut_network>(mapped_mig);
    fmt::print("KLUT network under Group ID:{}\n", gid.first);
    mockturtle::write_bench(klut,std::cout);
    gid2klut[gid.first]=klut;

  }
}

void Pass_mockturtle::do_work(LGraph *g) {
  LGBench b("pass.mockturtle");

  //absl::flat_hash_map<Node::Compact, std::pair<std::string,std::pair<int,int>>> cell_type;

  int cell_amount = 0;
  for(const auto &nid : g->forward()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished
    node.get_type(); //avoid warning
    cell_amount++;
/*
    auto name = node.get_type().get_name();
    int in_edges_num = 0;
    for(const auto &in_edge : node.inp_edges()) {
      in_edges_num++;
    }
    int out_edges_num = 0;
    for(const auto &out_edge : node.out_edges()) {
      out_edges_num++;
    }
    cell_type[node.get_compact()]=std::make_pair(name,std::make_pair(in_edges_num,out_edges_num));
*/
  }
  fmt::print("Pass: number of cells {}\n", cell_amount);
/*  fmt::print("Pass: number of cells {}\n", cell_type.size());

  for(auto const it:cell_type) {
    fmt::print("node_type:{} in_edges:{} out_edges:{}\n", it.second.first, it.second.second.first, it.second.second.second);
  }
*/
  fmt::print("Start partition...");

  lg_partition(g);

  for (const auto &group_id_it:group_boundary_set) {
    fmt::print("Group ID:{}\n", group_id_it.first);
    for (const auto &node_it:group_id_it.second) {
      fmt::print("Node identifier:{}\n", node_it);
    }
  }

  fmt::print("Creating k-LUT network...\n");

  create_LUT_network(g);

}
