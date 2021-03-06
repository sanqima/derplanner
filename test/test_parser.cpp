//
// Copyright (c) 2015 Alexander Shafranov shafranov@gmail.com
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
// claim that you wrote the original software. If you use this software
// in a product, an acknowledgment in the product documentation would be
// appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
// misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//

#include <string>
#include "unittestpp.h"
#include "derplanner/compiler/array.h"
#include "derplanner/compiler/lexer.h"
#include "derplanner/compiler/parser.h"
#include "derplanner/compiler/ast.h"

using namespace plnnrc;

// bring in parser implementation details.
namespace plnnrc
{
    extern ast::World*  parse_world(Parser*);
    extern ast::Expr*   parse_precond(Parser*);

    extern void         flatten(ast::Expr*);
    extern ast::Expr*   convert_to_nnf(const ast::Root* tree, ast::Expr* root);

    extern void         init_look_ahead(Parser*);
}

namespace
{
    std::string to_string(const ast::World* world)
    {
        std::string output;

        for (uint32_t i = 0; i < size(world->facts); ++i)
        {
            const ast::Fact* fact = world->facts[i];

            output.append(fact->name.str, fact->name.length);
            output.append("[");

            for (uint32_t j = 0; j < size(fact->params); ++j)
            {
                const ast::Data_Type* param = fact->params[j];

                const char* token_name = plnnrc::get_type_name(param->data_type);
                output.append(token_name);

                if (j < plnnrc::size(fact->params) - 1)
                {
                    output.append(", ");
                }
            }

            output.append("]");

            if (i < plnnrc::size(world->facts) - 1)
            {
                output.append(" ");
            }
        }

        return output;
    }

    void to_string(const ast::Expr* expr, std::string& output)
    {
        output += plnnrc::get_type_name(expr->type);
        Token_Value name = { 0, 0 };

        switch (expr->type)
        {
        case ast::Node_Var:
            name = static_cast<const ast::Var*>(expr)->name;
            break;
        case ast::Node_Func:
            name = static_cast<const ast::Func*>(expr)->name;
            break;
        default:
            break;
        }

        if (name.length > 0)
        {
            output += "[";
            output.append(name.str, name.length);
            output += "]";
        }

        if (expr->child)
        {
            output += "{ ";
            for (ast::Expr* child = expr->child; child != 0; child = child->next_sibling)
            {
                to_string(child, output);
                output += " ";
            }
            output += "}";
        }
    }

    struct Test_Compiler
    {
        ~Test_Compiler()
        {
            plnnrc::Memory_Stack::destroy(mem_scratch);
            plnnrc::Memory_Stack::destroy(mem_tree);
            memset(this, 0, sizeof(Test_Compiler));
        }

        Memory_Stack*   mem_tree;
        Memory_Stack*   mem_scratch;

        ast::Root       tree;
        Lexer           lexer;
        Parser          parser;
        Array<Error>    errors;
    };

    void init(Test_Compiler& compiler, const char* input)
    {
        compiler.mem_tree = plnnrc::Memory_Stack::create(1024);
        compiler.mem_scratch = plnnrc::Memory_Stack::create(1024);
        Array<Error> errors;
        init(compiler.errors, compiler.mem_tree, 16);
        init(&compiler.tree, &compiler.errors, compiler.mem_tree, compiler.mem_scratch);
        init(&compiler.lexer, input, compiler.mem_scratch);
        init(&compiler.parser, &compiler.lexer, &compiler.tree, &compiler.errors, compiler.mem_scratch);
        plnnrc::init_look_ahead(&compiler.parser);
    }

    TEST(world_parsing)
    {
        Test_Compiler compiler;
        init(compiler, "fact { f1(int32) f2(float, int32) f3() }");
        const char* expected = "f1[Int32] f2[Float, Int32] f3[]";
        ast::World* world = plnnrc::parse_world(&compiler.parser);
        std::string world_str = to_string(world);
        CHECK_EQUAL(expected, world_str.c_str());
    }

    void check_expr(const char* input, const char* expected)
    {
        Test_Compiler compiler;
        init(compiler, input);
        ast::Expr* expr = plnnrc::parse_precond(&compiler.parser);
        std::string actual;
        to_string(expr, actual);
        CHECK_EQUAL(expected, actual.c_str());
    }

    TEST(precondition_parsing)
    {
        check_expr("()", "And");
        check_expr("( f(x, y, z) )", "Func[f]{ Var[x] Var[y] Var[z] }");
        check_expr("( ~(a & b) | c )", "Or{ Not{ And{ Var[a] Var[b] } } Var[c] }");
        check_expr("( a & (b & c) )", "And{ Var[a] And{ Var[b] Var[c] } }");
    }

    void check_flattened_expr(const char* input, const char* expected)
    {
        Test_Compiler compiler;
        init(compiler, input);
        ast::Expr* expr = plnnrc::parse_precond(&compiler.parser);
        plnnrc::flatten(expr);
        std::string actual;
        to_string(expr, actual);
        CHECK_EQUAL(expected, actual.c_str());
    }

    TEST(minimizing_expression_depth)
    {
        check_flattened_expr("( ~(a & (b & (c | d | (e | f))) & (g & h)) )",
                             "Not{ And{ Var[a] Var[b] Or{ Var[c] Var[d] Var[e] Var[f] } Var[g] Var[h] } }");
    }

    void check_nnf_expr(const char* input, const char* expected)
    {
        Test_Compiler compiler;
        init(compiler, input);
        ast::Expr* expr = plnnrc::parse_precond(&compiler.parser);
        expr = plnnrc::convert_to_nnf(&compiler.tree, expr);
        std::string actual;
        to_string(expr, actual);
        CHECK_EQUAL(expected, actual.c_str());
    }

    TEST(nnf_conversion)
    {
        // trivial (already nnf).
        check_nnf_expr("(  )", "And");
        check_nnf_expr("( ~x & ~y )", "And{ Not{ Var[x] } Not{ Var[y] } }");
        // double-negation.
        check_nnf_expr("(   ~~x )", "Var[x]");
        check_nnf_expr("( ~~~~x )", "Var[x]");
        check_nnf_expr("(  ~~~x )", "Not{ Var[x] }");
        // De-Morgan's law.
        check_nnf_expr("( ~(x & (y | ~z)) )", "Or{ Not{ Var[x] } And{ Not{ Var[y] } Var[z] } }");
        // non logical ops considered literal.
        check_nnf_expr("( ~(x + y) )", "Not{ Plus{ Var[x] Var[y] } }");
        check_nnf_expr("( ~(x - 10.0) )", "Not{ Minus{ Var[x] Literal } }");
        check_nnf_expr("( ~((x + y) | ~~(x <= y)) )", "And{ Not{ Plus{ Var[x] Var[y] } } Not{ LessEqual{ Var[x] Var[y] } } }");
    }

    void check_dnf_expr(const char* input, const char* expected)
    {
        Test_Compiler compiler;
        init(compiler, input);
        ast::Expr* expr = plnnrc::parse_precond(&compiler.parser);
        expr = plnnrc::convert_to_dnf(&compiler.tree, expr);
        std::string actual;
        to_string(expr, actual);
        CHECK_EQUAL(expected, actual.c_str());
    }

    TEST(dnf_conversion)
    {
        // test trivial conversions.
        check_dnf_expr("( )", "Or");
        check_dnf_expr("( x )", "Or{ Var[x] }");
        check_dnf_expr("( ~x )", "Or{ Not{ Var[x] } }");
        check_dnf_expr("( a | b )", "Or{ Var[a] Var[b] }");
        // test expression is converted to nnf.
        check_dnf_expr("( a & ~(b | c) )", "Or{ And{ Var[a] Not{ Var[b] } Not{ Var[c] } } }");
        // distributive law.
        check_dnf_expr("( t1 & (~t2 | (t2 & t3 & t4)) )", "Or{ And{ Var[t1] Not{ Var[t2] } } And{ Var[t1] Var[t2] Var[t3] Var[t4] } }");
        check_dnf_expr("( q1 & (r1 | r2) & q2 & (r3 | r4) & q3 )",
                       "Or{ And{ Var[q1] Var[r1] Var[q2] Var[r3] Var[q3] } And{ Var[q1] Var[r1] Var[q2] Var[r4] Var[q3] } And{ Var[q1] Var[r2] Var[q2] Var[r3] Var[q3] } And{ Var[q1] Var[r2] Var[q2] Var[r4] Var[q3] } }");
        // non logical ops are considered to be trivial conjuncts.
        check_dnf_expr("( (a | 7.0) & ((c <= d) | (c + d)) )", "Or{ And{ Var[a] LessEqual{ Var[c] Var[d] } } And{ Var[a] Plus{ Var[c] Var[d] } } And{ Literal LessEqual{ Var[c] Var[d] } } And{ Literal Plus{ Var[c] Var[d] } } }");
    }
}
