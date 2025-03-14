#include "SreRuleEngine.h"
#include <sstream>
#include <cctype>
#include <algorithm>
#include <iostream>

// 如果使用 C++11 没有 std::make_unique，可以自己实现一个简单版本
template<typename T, typename... Args>
std::unique_ptr<T> sre_make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

// =============================
// 词法分析相关
// =============================
enum class SreTokenType {
    Identifier,     // 标识符（函数名或变量）
    StringLiteral,  // 字符串常量（单引号括起来的）
    Comma,
    LParen,
    RParen,
    And,
    Or,
    Not,
    End
};

struct SreToken {
    SreTokenType type;
    std::string text;
};

class SreLexer {
public:
    SreLexer(const std::string &input) : input_(input), pos_(0) {}

    SreToken nextToken() {
        skipWhitespace();
        if (pos_ >= input_.size()) return { SreTokenType::End, "" };

        char c = input_[pos_];
        if (std::isalpha(c) || c=='#' || c=='{' || c=='}') {
            std::string s;
            while (pos_ < input_.size() && (std::isalnum(input_[pos_]) || input_[pos_]=='#' || input_[pos_]=='{' || input_[pos_]=='}')) {
                s.push_back(input_[pos_++]);
            }
            std::string lower = s;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if(lower=="and") return { SreTokenType::And, s };
            if(lower=="or")  return { SreTokenType::Or, s };
            if(lower=="not") return { SreTokenType::Not, s };
            return { SreTokenType::Identifier, s };
        } else if (c=='\'') {
            pos_++; // 跳过开头的 '
            std::string s;
            while (pos_ < input_.size() && input_[pos_] != '\'') {
                s.push_back(input_[pos_++]);
            }
            pos_++; // 跳过结尾的 '
            return { SreTokenType::StringLiteral, s };
        } else if(c==',') {
            pos_++;
            return { SreTokenType::Comma, "," };
        } else if(c=='(') {
            pos_++;
            return { SreTokenType::LParen, "(" };
        } else if(c==')') {
            pos_++;
            return { SreTokenType::RParen, ")" };
        } else {
            throw std::runtime_error("Unexpected character: " + std::string(1, c));
        }
    }
private:
    void skipWhitespace() {
        while(pos_ < input_.size() && std::isspace(input_[pos_])) pos_++;
    }
    std::string input_;
    size_t pos_;
};

// =============================
// AST节点及求值接口
// 我们采用两种求值接口：evalString() 和 evalBool()
// 如果节点本质上是字符串型（如变量、常量），evalString() 返回实际字符串；若需要布尔值，则对字符串非空判 true
// 对于逻辑操作节点和函数节点，evalBool() 返回布尔值
// 如果不适用的接口调用将抛异常
// =============================
class SreASTNode {
public:
    virtual ~SreASTNode() {}
    // 返回布尔值，适用于逻辑表达式
    virtual bool evalBool(const SreContext &ctx, const std::unordered_map<std::string, SreFunction> &functions) {
        throw std::runtime_error("Not a boolean expression node");
    }
    // 返回字符串，适用于变量和字面量
    virtual std::string evalString(const SreContext &ctx, const std::unordered_map<std::string, SreFunction> &functions) {
        throw std::runtime_error("Not a string expression node");
    }
};

using SreASTNodePtr = std::unique_ptr<SreASTNode>;

// 逻辑节点（and, or, not），其子节点均要求为 boolean 表达式
class SreLogicalNode : public SreASTNode {
public:
    enum Operator { And, Or, Not };
    SreLogicalNode(Operator op, SreASTNodePtr left, SreASTNodePtr right = nullptr)
        : op_(op), left_(std::move(left)), right_(std::move(right)) {}

    bool evalBool(const SreContext &ctx, const std::unordered_map<std::string, SreFunction> &functions) override {
        switch(op_) {
            case And:
                return left_->evalBool(ctx, functions) && right_->evalBool(ctx, functions);
            case Or:
                return left_->evalBool(ctx, functions) || right_->evalBool(ctx, functions);
            case Not:
                return !left_->evalBool(ctx, functions);
        }
        throw std::runtime_error("Invalid logical operator");
    }
private:
    Operator op_;
    SreASTNodePtr left_;
    SreASTNodePtr right_;
};

// 变量或字符串常量节点：对于变量节点，返回上下文中对应的值；对于字符串常量节点，返回自身值
class SreValueNode : public SreASTNode {
public:
    // isVariable 表示是否为变量引用
    SreValueNode(const std::string &val, bool isVariable) : val_(val), isVariable_(isVariable) {}

    std::string evalString(const SreContext &ctx, const std::unordered_map<std::string, SreFunction> &/*functions*/) override {
        if (isVariable_) {
            auto it = ctx.find(val_);
            if (it == ctx.end()) {
                throw std::runtime_error("Variable not found: " + val_);
            }
            return it->second;
        } else {
            return val_;
        }
    }
    // 如果要求布尔值，则返回非空判断
    bool evalBool(const SreContext &ctx, const std::unordered_map<std::string, SreFunction> &functions) override {
        std::string s = evalString(ctx, functions);
        // 可根据需要调整，这里简单认为非空字符串为 true
        return !s.empty();
    }
private:
    std::string val_;
    bool isVariable_;
};

// 函数调用节点，函数调用用于返回布尔值（例如 contains）
class SreFunctionNode : public SreASTNode {
public:
    SreFunctionNode(const std::string &name, std::vector<SreASTNodePtr> args)
        : name_(name), args_(std::move(args)) {}

    bool evalBool(const SreContext &ctx, const std::unordered_map<std::string, SreFunction> &functions) override {
        std::vector<std::string> evaluatedArgs;
        for (auto &arg : args_) {
            // 对于函数调用参数，我们认为调用 evalString 得到实际值
            evaluatedArgs.push_back(arg->evalString(ctx, functions));
        }
        std::string lower = name_;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        auto it = functions.find(lower);
        if (it == functions.end()) {
            throw std::runtime_error("Function not found: " + name_);
        }
        return it->second(evaluatedArgs);
    }
private:
    std::string name_;
    std::vector<SreASTNodePtr> args_;
};

// =============================
// 解析器（递归下降解析器）
// =============================
class SreParser {
public:
    SreParser(SreLexer &lexer) : lexer_(lexer) {
        currentToken_ = lexer_.nextToken();
    }
    // 解析顶级表达式，返回一个 AST 节点，该表达式应为 boolean 表达式
    SreASTNodePtr parseExpression() {
        return parseOr();
    }
private:
    SreASTNodePtr parseOr() {
        SreASTNodePtr node = parseAnd();
        while (currentToken_.type == SreTokenType::Or) {
            consume(SreTokenType::Or);
            SreASTNodePtr right = parseAnd();
            node = sre_make_unique<SreLogicalNode>(SreLogicalNode::Or, std::move(node), std::move(right));
        }
        return node;
    }
    SreASTNodePtr parseAnd() {
        SreASTNodePtr node = parseNot();
        while (currentToken_.type == SreTokenType::And) {
            consume(SreTokenType::And);
            SreASTNodePtr right = parseNot();
            node = sre_make_unique<SreLogicalNode>(SreLogicalNode::And, std::move(node), std::move(right));
        }
        return node;
    }
    SreASTNodePtr parseNot() {
        if (currentToken_.type == SreTokenType::Not) {
            consume(SreTokenType::Not);
            SreASTNodePtr operand = parsePrimary();
            return sre_make_unique<SreLogicalNode>(SreLogicalNode::Not, std::move(operand));
        }
        return parsePrimary();
    }
    SreASTNodePtr parsePrimary() {
        if (currentToken_.type == SreTokenType::LParen) {
            consume(SreTokenType::LParen);
            SreASTNodePtr node = parseExpression();
            consume(SreTokenType::RParen);
            return node;
        } else if (currentToken_.type == SreTokenType::Identifier) {
            std::string name = currentToken_.text;
            consume(SreTokenType::Identifier);
            if (currentToken_.type == SreTokenType::LParen) {
                // 函数调用
                consume(SreTokenType::LParen);
                std::vector<SreASTNodePtr> args;
                if (currentToken_.type != SreTokenType::RParen) {
                    args.push_back(parseExpression());
                    while (currentToken_.type == SreTokenType::Comma) {
                        consume(SreTokenType::Comma);
                        args.push_back(parseExpression());
                    }
                }
                consume(SreTokenType::RParen);
                return sre_make_unique<SreFunctionNode>(name, std::move(args));
            } else {
                // 变量引用，例如 #{a}，这里如果包含 '#' 或 '{' 则视为变量
                bool isVar = (name.find('#') != std::string::npos);
                std::string varName = name;
                // 去掉特殊符号
                varName.erase(std::remove(varName.begin(), varName.end(), '#'), varName.end());
                varName.erase(std::remove(varName.begin(), varName.end(), '{'), varName.end());
                varName.erase(std::remove(varName.begin(), varName.end(), '}'), varName.end());
                return sre_make_unique<SreValueNode>(varName, isVar);
            }
        } else if (currentToken_.type == SreTokenType::StringLiteral) {
            std::string s = currentToken_.text;
            consume(SreTokenType::StringLiteral);
            return sre_make_unique<SreValueNode>(s, false);
        } else {
            throw std::runtime_error("Unexpected token: " + currentToken_.text);
        }
    }
    void consume(SreTokenType type) {
        if (currentToken_.type != type) {
            throw std::runtime_error("Expected token type mismatch");
        }
        currentToken_ = lexer_.nextToken();
    }
    SreLexer &lexer_;
    SreToken currentToken_;
};

// =============================
// SreRuleEngine 成员函数实现
// =============================
SreRuleEngine::SreRuleEngine() {
    initBuiltInFunctions();
}

SreRuleEngine::~SreRuleEngine() {}

void SreRuleEngine::registerFunction(const std::string &name, SreFunction func) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    functions_[lower] = func;
}

void SreRuleEngine::initBuiltInFunctions() {
    // 内置函数 contains(#{var}, 'substring')
    registerFunction("contains", [](const std::vector<std::string>& args) -> bool {
        if (args.size() != 2) throw std::runtime_error("contains requires 2 arguments");
        return args[0].find(args[1]) != std::string::npos;
    });
    // 内置函数 containsAny(#{var}, 's1', 's2', ...)
    registerFunction("containsany", [](const std::vector<std::string>& args) -> bool {
        if (args.size() < 2) throw std::runtime_error("containsAny requires at least 2 arguments");
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[0].find(args[i]) != std::string::npos) return true;
        }
        return false;
    });
}

bool SreRuleEngine::evaluate(const std::string &expression, const SreContext &ctx) {
    SreLexer lexer(expression);
    SreParser parser(lexer);
    SreASTNodePtr root = parser.parseExpression();
    // 顶层表达式应返回 boolean
    return root->evalBool(ctx, functions_);
}
