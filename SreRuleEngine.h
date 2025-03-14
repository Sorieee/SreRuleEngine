#ifndef SRE_RULE_ENGINE_H
#define SRE_RULE_ENGINE_H

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>
#include <stdexcept>

// 上下文：存储变量值（简单采用字符串映射）
using SreContext = std::unordered_map<std::string, std::string>;

// 内置函数类型：接收字符串参数列表，返回 bool
using SreFunction = std::function<bool(const std::vector<std::string>&)>;

class SreRuleEngine {
public:
    SreRuleEngine();
    ~SreRuleEngine();

    // 注册函数，函数名会转为小写保存
    void registerFunction(const std::string &name, SreFunction func);

    // 评估表达式，表达式返回布尔值
    bool evaluate(const std::string &expression, const SreContext &ctx);

private:
    // 内部存储函数映射
    std::unordered_map<std::string, SreFunction> functions_;

    // 初始化内置函数
    void initBuiltInFunctions();

    // 以下为内部解析和求值相关类和函数，声明放在 SreRuleEngine.cpp 中
};

#endif // SRE_RULE_ENGINE_H
