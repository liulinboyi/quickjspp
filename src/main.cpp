//
// Created by 81492 on 2021/4/2.
//

#include "quickjspp.hpp"
#include <iostream>
#include <cstring>

void println(const std::string &str) { std::cout << str << std::endl; }

int main() {
    using namespace std;
    using namespace qjs;
    Runtime runtime;
    Context context(runtime);
    try {
        // 将cpp函数挂到JavaScript引擎中去
        auto &module = context.addModule("MyModule");
        module.function<&println>("println");
        // 引入已经挂到JavaScript引擎的MyModule
        context.eval("import * as my from 'MyModule'; globalThis.my = my;", "<import>", JS_EVAL_TYPE_MODULE);

        println("-----------------------------Dividing line--------------------------------");

        string expr5 = "(new Promise(resolve => resolve()).then(() => {this.a = 100;my.println(`js this.a: ${this.a}`)}),function () {return this.a})";

        Value t = context.evaluateScript(expr5);

        cout << "expr5 Execution Result:" << t.as<string>() << endl; // 输出看一下返回的是什么

        println("-----------------------------Dividing line--------------------------------");

        string expr3 = expr5;

        // 这一句相当于在控制台，按回车，运行这段代码
        /**
        var fn = (new Promise(resolve => resolve()).then(() => {this.a = 100;console.log(`js this.a: ${this.a}`)}),function () {return this.a})
         */
        auto fn = (std::function<Value()>) context.evaluateScript(expr3); // JavaScript引擎返回一个JavaScript函数（宏任务）

        // 这一句相当于在控制台，按回车，运行这段代码
        /**
         fn()
         */
        Value fnRes = fn(); // 执行JavaScript引擎返回的函数（宏任务）

        cout << "fnRes: " << fnRes.toJSON() << endl;

    }
    catch (qjs::exception) {
        cout << "出错了" << endl;
        auto exc = context.getException();
        std::cerr << (exc.isError() ? "Error: " : "Throw: ") << (std::string) exc << std::endl;
        if ((bool) exc["stack"])
            std::cerr << (std::string) exc["stack"] << std::endl;
        return 1;
    }
}
