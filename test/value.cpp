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
        auto &module = context.addModule("MyModule");
        module.function<&println>("println");
        context.eval("import * as my from 'MyModule'; globalThis.my = my;", "<import>", JS_EVAL_TYPE_MODULE);


        auto val1 = context.newValue(321);
        auto val2 = context.newValue(123);
        assert(val1 != val2);

        val1 = val2;
        assert(val1 == val2);

        val1 = context.newValue(123);
        assert(val1 == val2);

        val1 = std::move(val2);
        assert(val1 != val2);

        assert(val1.as<std::string>() == "123");

        assert((double) val1 == 123.0);

        val2 = context.newValue((std::string) "123");
        assert(val1 != val2);

        context.global()["val1"] = val1;
        context.global()["val2"] = val2;


        assert((bool) context.eval("val1 !== val2"));
        assert((bool) context.eval("val1 == val2"));

        //

        val1 = context.newObject();
        val1["a"] = "1";
        val2 = context.newObject();
        val2["a"] = "1";

        assert(val1 != val2);

        context.global()["val1"] = val1;
        context.global()["val2"] = val2;
        context.global()["temp"] = context.newValue(0);

        assert((bool) context.eval("val1 !== val2"));
        assert((bool) context.eval("val1 != val2"));
        assert((bool) context.eval("JSON.stringify(val1) === JSON.stringify(val2)"));

        Value one = val1["a"];
        cout << (int) one << endl;
        assert((int) one == 1);

        string expr = "1+5";
        Value res = context.eval(expr);

        int re = JS_VALUE_GET_INT(res.v);

        cout << expr << ": " << re << endl;

        println("-----------------------------�ָ���--------------------------------");

        string expr3 = "(new Promise(resolve => resolve()).then(() => {this.a = 100;my.println(`js this.a: ${this.a}`)}),function () {return this.a})";

//        {
//            // �����õ���evalִ�д��룬���������������΢����Ч����Ҫʹ��evaluateScript��ִ��<script>����Ƭ�βŲ��Ч��
//            Value t = context.eval(expr3);
//            cout << (string) t << endl;
//            cout << t.as<string>() << endl;
//            auto a = js_traits<string>::unwrap(context.ctx, t.v);
//            cout << a << endl;
//            js_std_loop(context.ctx); // ��һ�������������������������������ǰ��Ĵ���һ����ͬһ����������ִ�У����������У���ʵevaluateScriptҲ�Ǽ�����һ��������ڴ���ƴ���
//
//            auto fn = (std::function<Value()>) context.evaluateScript(expr3);
//
//            Value fnRes = fn(); // ��������ֵ
//
//            cout << "fnRes: " << fnRes.toJSON() << endl;
//        }


        Value t = context.evaluateScript(expr3);

        cout << t.as<string>() << endl; // �����һ�·��ص���ʲô

        println("------------------------�ָ���--------------------------------");

        auto fn = (std::function<Value()>) context.evaluateScript(expr3);

        Value fnRes = fn(); // ��������ֵ

        cout << "fnRes: " << fnRes.toJSON() << endl;

//
//        string expr5 = "(function add(x,y) {return x + y})";
//
//        auto fn1 = (std::function<Value(int, int)>) context.eval(expr5);
//
//        Value fnRes1 = fn1(2, 3);
//
//        cout << context.global().toJSON() << endl;
    }
    catch (qjs::exception) {
        cout << "������" << endl;
        auto exc = context.getException();
        std::cerr << (exc.isError() ? "Error: " : "Throw: ") << (std::string) exc << std::endl;
        if ((bool) exc["stack"])
            std::cerr << (std::string) exc["stack"] << std::endl;
        return 1;
    }
}
