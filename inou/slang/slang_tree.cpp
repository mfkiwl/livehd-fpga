
#include <charconv>

#include "slang/compilation/Compilation.h"
#include "slang/compilation/Definition.h"
#include "slang/symbols/ASTSerializer.h"
#include "slang/symbols/CompilationUnitSymbols.h"
#include "slang/symbols/InstanceSymbols.h"
#include "slang/symbols/PortSymbols.h"
#include "slang/syntax/SyntaxPrinter.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/types/Type.h"

#include "fmt/format.h"
#include "iassert.hpp"
#include "slang_tree.hpp"

Slang_tree::Slang_tree() {
}

void Slang_tree::setup() {
  parsed_lnasts.clear();
}


std::string_view Slang_tree::create_lnast_tmp() {
  ++tmp_var_cnt;
  return lnast->add_string(absl::StrCat("___", std::to_string(tmp_var_cnt)));
}

std::string_view Slang_tree::create_lnast(int val) {
  return lnast->add_string(std::to_string(val));
}

std::string_view Slang_tree::create_lnast(const std::string &val) {
  return lnast->add_string(val);
}

std::string_view Slang_tree::create_lnast(std::string_view val) {
  return lnast->add_string(val);
}

std::string_view Slang_tree::create_lnast_var(std::string_view val) {

  std::string_view var_name;

  const auto &it = net2attr.find(val);
  if (it == net2attr.end())
    return lnast->add_string(val);

  if (it->second == Net_attr::Input) {
    var_name = lnast->add_string(absl::StrCat("$", val));
  }else if (it->second == Net_attr::Output) {
    var_name = lnast->add_string(absl::StrCat("%", val));
  }else if (it->second == Net_attr::Register) {
    var_name = lnast->add_string(absl::StrCat("#", val));
  }else{
    var_name = lnast->add_string(val);
  }

  return var_name;
}

void Slang_tree::new_lnast(std::string_view name) {

  lnast = std::make_unique<Lnast>(name);
  lnast->set_root(Lnast_node(Lnast_ntype::create_top()));

  auto node_stmts = Lnast_node::create_stmts("___stmts");
  idx_stmts = lnast->add_child(lnast->get_root(), node_stmts);

  net2attr.clear();

  tmp_var_cnt = 0;
}

void Slang_tree::process_root(const slang::RootSymbol &root) {

  auto topInstances = root.topInstances;
  for (auto inst : topInstances) {
    fmt::print("slang_tree top:{}\n", inst->name);

    Slang_tree tree;
    I(!has_lnast(inst->name)); // top level should not be already (may sub instances)
    auto ok = tree.process_top_instance(*inst);
    if (!ok) {
      Pass::info("unable to process top module:{}\n", inst->name);
    }
  }
}

std::vector<std::shared_ptr<Lnast>> Slang_tree::pick_lnast() {
  std::vector<std::shared_ptr<Lnast>> v;

  for(auto &l:parsed_lnasts) {
    if (l.second) // do not push null ptr
      v.emplace_back(l.second);
  }

  parsed_lnasts.clear();

  return v;
}

bool Slang_tree::process_top_instance(const slang::InstanceSymbol& symbol) {
  const auto& def = symbol.getDefinition();

  // Instance bodies are all the same, so if we've visited this one
  // already don't bother doing it again.
  if (parsed_lnasts.contains(def.name)) {
    fmt::print("slang_tree module:{} already parsed\n", def.name);
    return false;
  }

  I(symbol.isModule()); // modules are fine. Interfaces are a TODO

  I(lnast == nullptr);
  fmt::print("definition:{}\n", def.name);

  parsed_lnasts.emplace(def.name, nullptr); // insert to avoid recursion (reinsert at the end)

  new_lnast(def.name);

  symbol.resolvePortConnections();
  for (const auto& p : symbol.body.getPortList()) {
    if (p->kind == slang::SymbolKind::Port) {
      const auto &port = p->as<slang::PortSymbol>();

      I(port.defaultValue == nullptr);  // give me a case to DEBUG

      std::string_view var_name;
      if (port.direction == slang::ArgumentDirection::In) {
        var_name = create_lnast(absl::StrCat("$", port.name));
        net2attr.emplace(port.name, Net_attr::Input);
      }else{
        var_name = create_lnast(absl::StrCat("%", port.name));
        net2attr.emplace(port.name, Net_attr::Output);
      }

      create_declare_bits_stmts(var_name, port.getType().isSigned(), port.getType().getBitWidth());
      fmt::print("TODO: set bit declaration position\n");

    } else if (p->kind == slang::SymbolKind::InterfacePort) {
      const auto& port = p->as<slang::InterfacePortSymbol>();
      (void)port;

      fmt::print("port:{} FIXME\n", p->name);
    } else {
      I(false);  // What other type?
    }
  }

  for(const auto &member:symbol.body.members()) {
    if (member.kind == slang::SymbolKind::Port) {
      // already done
    }else if (member.kind == slang::SymbolKind::Net) {
      // No need, handled in assignments directly
    }else if (member.kind == slang::SymbolKind::ContinuousAssign) {
      const auto &ca = member.as<slang::ContinuousAssignSymbol>();
      const auto &as = ca.getAssignment();
      bool ok = process(as.as<slang::AssignmentExpression>());
      if (!ok) {
        lnast = nullptr;
        return false;
      }
    }else if (member.kind == slang::SymbolKind::ProceduralBlock) {
      const auto &pbs = member.as<slang::ProceduralBlockSymbol>();

      if (pbs.procedureKind == slang::ProceduralBlockKind::Always) {
        const auto &stmt = pbs.getBody();

        if (stmt.kind == slang::StatementKind::Timed) {
          Pass::info("always with sensitivity list at {} pos:{}, assuming always_comb", member.location.buffer().getId(), member.location.offset());
          const auto& timed = stmt.as<slang::TimedStatement>();
          if (timed.stmt.kind == slang::StatementKind::Block) {
            const auto &block = timed.stmt.as<slang::BlockStatement>();
            I(block.getStatements().kind == slang::StatementKind::List);
            for (const auto &bstmt : block.getStatements().as<slang::StatementList>().list) {
              if (bstmt->kind == slang::StatementKind::ExpressionStatement) {
                const auto &expr = bstmt->as<slang::ExpressionStatement>().expr;
                bool ok = process(expr.as<slang::AssignmentExpression>());
                if (!ok) {
                  lnast = nullptr;
                  return false;
                }
              } else {
                fmt::print("TODO: handle kind {}\n", bstmt->kind);
              }
            }
          }

        }else{
          fmt::print("FIXME: missing sensitivity type\n");
        }
      }
    }else {
      fmt::print("FIXME: missing body type\n");
    }
  }

  lnast->dump();

  parsed_lnasts.insert_or_assign(def.name, lnast);
  lnast = nullptr;

  return true;
}

bool Slang_tree::process(const slang::AssignmentExpression& expr) {

  const auto &lhs = expr.left();

  std::string_view var_name;
  bool dest_var_sign;
  int  dest_var_bits;

  std::string_view dest_max_bit;
  std::string_view dest_min_bit;

  if (lhs.kind == slang::ExpressionKind::NamedValue) {
    const auto& var = lhs.as<slang::NamedValueExpression>();
    var_name = create_lnast_var(var.symbol.name);
    dest_var_sign = var.type->isSigned();
    dest_var_bits = var.type->getBitWidth();
    I(!var.type->isStruct()); // FIXME: structs
  }else{
    I(lhs.kind == slang::ExpressionKind::RangeSelect);
    const auto &rs = lhs.as<slang::RangeSelectExpression>();
    I(rs.value().kind == slang::ExpressionKind::NamedValue);

    const auto& var = rs.value().as<slang::NamedValueExpression>();
    var_name = create_lnast_var(var.symbol.name);
    dest_var_sign = var.type->isSigned();
    dest_var_bits = var.type->getBitWidth();
    I(!var.type->isStruct()); // FIXME: structs

    dest_max_bit = process_expression(rs.left());
    dest_min_bit = process_expression(rs.right());
  }

  auto it = net2attr.find(var_name);
  if (it == net2attr.end()) {
    net2attr.emplace(var_name, Net_attr::Local);
    create_declare_bits_stmts(var_name, dest_var_sign, dest_var_bits);
  }

  std::string_view rhs_var;
  if (dest_min_bit.empty() && dest_max_bit.empty()) {
    rhs_var = process_expression(expr.right());
  }else{
    auto expr_var     = process_expression(expr.right());

    auto mask_h_var   = create_mask_stmts(dest_max_bit); // (1<<H)-1
    auto mask_l_var   = create_not_stmts(create_mask_stmts(dest_min_bit)); // ~((1<<L)-1)
    auto mask_var     = create_and_stmts(mask_h_var, mask_l_var); // ((1<<H)-1) & (~((1<<L)-1))
    auto mask_not_var = create_not_stmts(mask_var);

    auto adj_rhs_var  = create_shift_left_stmts(expr_var   , dest_min_bit);
    auto updated_rhs  = create_and_stmts(adj_rhs_var, mask_var);
    auto rest_rhs     = create_and_stmts(var_name   , mask_not_var);

    rhs_var = create_or_stmts(updated_rhs, rest_rhs);
  }

  create_dp_assign_stmts(var_name, rhs_var);

  return true;
}

// Return a __tmp for (1<<expr)-1
std::string_view Slang_tree::create_mask_stmts(std::string_view dest_max_bit) {
  if (dest_max_bit.empty())
    return dest_max_bit;

  // some fast precomputed values
  if (std::isdigit(dest_max_bit[0])) {
    int value=0;
    const auto res = std::from_chars(dest_max_bit.data(), dest_max_bit.data() + dest_max_bit.size(), value);
    if (res.ec == std::errc() && value<63 && value>=0) {
      uint64_t v = (1ULL<<value)-1;
      return lnast->add_string(std::to_string(v));
    }
  }

  auto shl_var    = create_shift_left_stmts("1", dest_max_bit);
  auto mask_h_var = create_minus_stmts(shl_var, "1");

  return mask_h_var;
}

std::string_view Slang_tree::create_not_stmts(std::string_view var_name) {
  if (var_name.empty())
    return var_name;

  auto res_var = create_lnast_tmp();
  auto not_idx = lnast->add_child(idx_stmts, Lnast_node::create_not("NOT"));
  lnast->add_child(not_idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(var_name[0]))
    lnast->add_child(not_idx, Lnast_node::create_const(var_name));
  else
    lnast->add_child(not_idx, Lnast_node::create_ref(var_name));

  return res_var;
}

std::string_view Slang_tree::create_and_stmts(std::string_view a_var, std::string_view b_var) {
  if (a_var.empty())
    return b_var;
  if (b_var.empty())
    return a_var;

  auto res_var = create_lnast_tmp();
  auto and_idx = lnast->add_child(idx_stmts, Lnast_node::create_and("AND"));
  lnast->add_child(and_idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(a_var[0]))
    lnast->add_child(and_idx, Lnast_node::create_const(a_var));
  else
    lnast->add_child(and_idx, Lnast_node::create_ref(a_var));
  if (std::isdigit(b_var[0]))
    lnast->add_child(and_idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(and_idx, Lnast_node::create_ref(b_var));

  return res_var;
}

std::string_view Slang_tree::create_or_stmts(std::string_view a_var, std::string_view b_var) {
  if (a_var.empty())
    return b_var;
  if (b_var.empty())
    return a_var;

  auto res_var = create_lnast_tmp();
  auto or_idx = lnast->add_child(idx_stmts, Lnast_node::create_or("OR"));
  lnast->add_child(or_idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(a_var[0]))
    lnast->add_child(or_idx, Lnast_node::create_const(a_var));
  else
    lnast->add_child(or_idx, Lnast_node::create_ref(a_var));
  if (std::isdigit(b_var[0]))
    lnast->add_child(or_idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(or_idx, Lnast_node::create_ref(b_var));

  return res_var;
}

std::string_view Slang_tree::create_xor_stmts(std::string_view a_var, std::string_view b_var) {
  if (a_var.empty())
    return b_var;
  if (b_var.empty())
    return a_var;

  auto res_var = create_lnast_tmp();
  auto or_idx = lnast->add_child(idx_stmts, Lnast_node::create_xor("XOR"));
  lnast->add_child(or_idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(a_var[0]))
    lnast->add_child(or_idx, Lnast_node::create_const(a_var));
  else
    lnast->add_child(or_idx, Lnast_node::create_ref(a_var));
  if (std::isdigit(b_var[0]))
    lnast->add_child(or_idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(or_idx, Lnast_node::create_ref(b_var));

  return res_var;
}

std::string_view Slang_tree::create_shift_left_stmts(std::string_view a_var, std::string_view b_var) {
  if (a_var.empty())
    return a_var;
  if (b_var.empty())
    return a_var;

  auto res_var = create_lnast_tmp();
  auto shl_idx = lnast->add_child(idx_stmts, Lnast_node::create_shift_left("SHL"));
  lnast->add_child(shl_idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(a_var[0]))
    lnast->add_child(shl_idx, Lnast_node::create_const(a_var));
  else
    lnast->add_child(shl_idx, Lnast_node::create_ref(a_var));
  if (std::isdigit(b_var[0]))
    lnast->add_child(shl_idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(shl_idx, Lnast_node::create_ref(b_var));

  return res_var;
}

void Slang_tree::create_dp_assign_stmts(std::string_view lhs_var, std::string_view rhs_var) {
  I(lhs_var.size());
  I(rhs_var.size());

  auto idx_assign = lnast->add_child(idx_stmts, Lnast_node::create_dp_assign(""));
  lnast->add_child(idx_assign, Lnast_node::create_ref(lhs_var));
  if (std::isdigit(rhs_var[0]))
    lnast->add_child(idx_assign, Lnast_node::create_const(rhs_var));
  else
    lnast->add_child(idx_assign, Lnast_node::create_ref(rhs_var));
}

void Slang_tree::create_declare_bits_stmts(std::string_view a_var, bool is_signed, int bits) {
  auto tmp_var = create_lnast_tmp();

  auto idx_dot = lnast->add_child(idx_stmts, Lnast_node::create_select(""));
  lnast->add_child(idx_dot, Lnast_node::create_ref(tmp_var));
  lnast->add_child(idx_dot, Lnast_node::create_ref(a_var));

  if (is_signed) {
    lnast->add_child(idx_dot, Lnast_node::create_const("__sbits"));
  }else{
    lnast->add_child(idx_dot, Lnast_node::create_const("__ubits"));
  }
  auto idx_assign = lnast->add_child(idx_stmts, Lnast_node::create_assign(""));
  lnast->add_child(idx_assign, Lnast_node::create_ref(tmp_var));
  lnast->add_child(idx_assign, Lnast_node::create_const(create_lnast(bits)));
}

std::string_view Slang_tree::create_minus_stmts(std::string_view a_var, std::string_view b_var) {
  if (b_var.empty())
    return a_var;

  auto res_var = create_lnast_tmp();
  auto sub_idx = lnast->add_child(idx_stmts, Lnast_node::create_minus("MINUS"));
  lnast->add_child(sub_idx, Lnast_node::create_ref(res_var));
  if (a_var.empty()) {
    lnast->add_child(sub_idx, Lnast_node::create_const("0"));
  }else{
    if (std::isdigit(a_var[0]))
      lnast->add_child(sub_idx, Lnast_node::create_const(a_var));
    else
      lnast->add_child(sub_idx, Lnast_node::create_ref(a_var));
  }
  if (std::isdigit(b_var[0]))
    lnast->add_child(sub_idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(sub_idx, Lnast_node::create_ref(b_var));

  return res_var;
}

std::string_view Slang_tree::create_plus_stmts(std::string_view a_var, std::string_view b_var) {
  if (a_var.empty())
    return b_var;
  if (b_var.empty())
    return a_var;

  auto res_var = create_lnast_tmp();
  auto add_idx = lnast->add_child(idx_stmts, Lnast_node::create_plus("PLUS"));
  lnast->add_child(add_idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(a_var[0]))
    lnast->add_child(add_idx, Lnast_node::create_const(a_var));
  else
    lnast->add_child(add_idx, Lnast_node::create_ref(a_var));
  if (std::isdigit(b_var[0]))
    lnast->add_child(add_idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(add_idx, Lnast_node::create_ref(b_var));

  return res_var;
}

std::string_view Slang_tree::create_mult_stmts(std::string_view a_var, std::string_view b_var) {
  if (a_var.empty() || a_var == "1")
    return b_var;
  if (b_var.empty() || b_var == "1")
    return a_var;

  auto res_var = create_lnast_tmp();
  auto idx = lnast->add_child(idx_stmts, Lnast_node::create_mult("MULT"));
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(a_var[0]))
    lnast->add_child(idx, Lnast_node::create_const(a_var));
  else
    lnast->add_child(idx, Lnast_node::create_ref(a_var));
  if (std::isdigit(b_var[0]))
    lnast->add_child(idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_ref(b_var));

  return res_var;
}

std::string_view Slang_tree::create_div_stmts(std::string_view a_var, std::string_view b_var) {
  if (b_var.empty() || b_var == "1")
    return a_var;

  auto res_var = create_lnast_tmp();
  auto idx = lnast->add_child(idx_stmts, Lnast_node::create_div("DIV"));
  lnast->add_child(idx, Lnast_node::create_ref(res_var));

  if (a_var.empty()) {
    lnast->add_child(idx, Lnast_node::create_const("1"));
  }else{
    if (std::isdigit(a_var[0]))
      lnast->add_child(idx, Lnast_node::create_const(a_var));
    else
      lnast->add_child(idx, Lnast_node::create_ref(a_var));
  }

  if (std::isdigit(b_var[0]))
    lnast->add_child(idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_ref(b_var));

  return res_var;
}

std::string_view Slang_tree::create_mod_stmts(std::string_view a_var, std::string_view b_var) {
  I(a_var.size() && b_var.size());

  auto res_var = create_lnast_tmp();
  auto idx = lnast->add_child(idx_stmts, Lnast_node::create_mod("MOD"));
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(a_var[0]))
    lnast->add_child(idx, Lnast_node::create_const(a_var));
  else
    lnast->add_child(idx, Lnast_node::create_ref(a_var));
  if (std::isdigit(b_var[0]))
    lnast->add_child(idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_ref(b_var));

  return res_var;
}

std::string_view Slang_tree::process_expression(const slang::Expression& expr) {

  if (expr.kind == slang::ExpressionKind::NamedValue) {
    const auto &nv = expr.as<slang::NamedValueExpression>();
    return create_lnast_var(nv.symbol.name);
  }

  if (expr.kind == slang::ExpressionKind::IntegerLiteral) {
    const auto &il = expr.as<slang::IntegerLiteral>();
    auto svint = il.getValue();
    slang::SmallVectorSized<char, 32> buffer;
    if (!svint.hasUnknown() && svint.getMinRepresentedBits() < 8) {
      svint.writeTo(buffer, slang::LiteralBase::Decimal, false);
      return create_lnast(std::string(buffer.begin(), buffer.end()));
    }

    svint.writeTo(buffer, slang::LiteralBase::Hex, false);
    return create_lnast(std::string("0x") + std::string(buffer.begin(), buffer.end()));
  }

  if (expr.kind == slang::ExpressionKind::BinaryOp) {
    const auto &op = expr.as<slang::BinaryExpression>();
    auto lhs = process_expression(op.left());
    auto rhs = process_expression(op.right());
    std::string_view res_var;
    switch (op.op) {
      case slang::BinaryOperator::Add:        return create_plus_stmts(lhs, rhs);
      case slang::BinaryOperator::Subtract:   return create_minus_stmts(lhs, rhs);
      case slang::BinaryOperator::Multiply:   return create_mult_stmts(lhs,rhs);
      case slang::BinaryOperator::Divide:     return create_div_stmts(lhs, rhs);
      case slang::BinaryOperator::Mod:        return create_mod_stmts(lhs, rhs);
      case slang::BinaryOperator::BinaryAnd:  return create_and_stmts(lhs, rhs);
      case slang::BinaryOperator::BinaryOr:   return create_or_stmts(lhs,rhs);
      case slang::BinaryOperator::BinaryXor:  return create_xor_stmts(lhs, rhs);
      case slang::BinaryOperator::BinaryXnor: return create_not_stmts(create_xor_stmts(lhs, rhs));
      default: {
        fmt::print("FIXME unimplemented binary operator\n");
        break;
      }
    }
    return "fix_binary_op";
  }

  fmt::print("FIXME still unimplemented Expression kind\n");

  return "FIXME_op";
}
