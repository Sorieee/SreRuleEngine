提供一个基于字符串的规则引擎框架
使用参考main.cpp
```c++
SreRuleEngine engine;

    // 设置上下文变量
    SreContext ctx;
    ctx["a"] = "你好";
    ctx["b"] = "hello, world, xxxx";

    // 示例表达式：使用内置函数 contains 和 containsAny
    std::string expr = "(contains(#{a}, '好') or contains(#{a}, 'yyy') ) and containsAny(#{b}, 'xxx', '22')";

    bool result = engine.evaluate(expr, ctx);
    std::cout << "Expression result: " << std::boolalpha << result << std::endl;

    expr = "(contains(#{a}, 'xxx')) and containsAny(#{b}, 'xxx', '22')";

    result = engine.evaluate(expr, ctx);
    std::cout << "Expression result: " << std::boolalpha << result << std::endl;
    expr = "(not contains(#{a}, 'xxx')) and containsAny(#{b}, 'xxxx', '22')";

    result = engine.evaluate(expr, ctx);
    std::cout << "Expression result: " << std::boolalpha << result << std::endl;

    expr = "(contains(#{a}, 'contains')) and containsAny(#{b}, 'xxxx', '22')";

    result = engine.evaluate(expr, ctx);
    std::cout << "Expression result: " << std::boolalpha << result << std::endl;
```