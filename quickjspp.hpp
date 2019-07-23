#pragma once

extern "C" {
#include "quickjs/quickjs.h"
#include "quickjs/quickjs-libc.h"
}

#include <algorithm>
#include <tuple>
#include <functional>
#include <unordered_map>
#include <cassert>
#include <memory>
#include <cstddef>

namespace qjs {


    template <typename T>
    struct allocator
    {
        JSRuntime * rt;
        using value_type = T;
        using propagate_on_container_move_assignment = std::true_type;
        using propagate_on_container_copy_assignment = std::true_type;

        constexpr allocator(JSRuntime * rt) noexcept : rt{rt}
        {}

        allocator(JSContext * ctx) noexcept : rt{JS_GetRuntime(ctx)}
        {}

        template <class U>
        constexpr allocator(const allocator<U>& other) noexcept : rt{other.rt}
        {}

        [[nodiscard]] T * allocate(std::size_t n)
        {
            if(auto p = static_cast<T *>(js_malloc_rt(rt, n * sizeof(T)))) return p;
            throw std::bad_alloc();
        }

        void deallocate(T * p, std::size_t) noexcept
        { js_free_rt(rt, p); }

        template <class U>
        bool operator == (const allocator<U>& other) const { return rt == other.rt; }
        template <class U>
        bool operator != (const allocator<U>& other) const { return rt != other.rt; }
    };

    namespace detail {
        class exception {};

        template <typename R>
        struct js_traits
        {
            //static R unwrap(JSContext * ctx, JSValueConst v);
            //static JSValue wrap(JSContext * ctx, R value);
        };

        // identity
        template <>
        struct js_traits<JSValue>
        {
            static JSValue unwrap(JSContext * ctx, JSValueConst v) noexcept
            {
                return JS_DupValue(ctx, v);
            }

            static JSValue wrap(JSContext * ctx, JSValue v) noexcept
            {
                return v;
            }
        };

        template <>
        struct js_traits<int32_t>
        {
            static int32_t unwrap(JSContext * ctx, JSValueConst v)
            {
                int32_t r;
                if(JS_ToInt32(ctx, &r, v))
                    throw exception{};
                return r;
            }

            static JSValue wrap(JSContext * ctx, int32_t i) noexcept
            {
                return JS_NewInt32(ctx, i);
            }
        };

        template <>
        struct js_traits<bool>
        {
            static bool unwrap(JSContext * ctx, JSValueConst v) noexcept
            {
                return JS_ToBool(ctx, v);
            }

            static JSValue wrap(JSContext * ctx, bool i) noexcept
            {
                return JS_NewBool(ctx, i);
            }
        };

        template <>
        struct js_traits<void>
        {
            static void unwrap(JSContext * ctx, JSValueConst undefined)
            {
                if(!JS_IsUndefined(undefined))
                    throw exception{};
            }
        };

        template <>
        struct js_traits<double>
        {
            static double unwrap(JSContext * ctx, JSValueConst v)
            {
                double r;
                if(JS_ToFloat64(ctx, &r, v))
                    throw exception{};
                return r;
            }

            static JSValue wrap(JSContext * ctx, double i) noexcept
            {
                return JS_NewFloat64(ctx, i);
            }
        };

        class js_string : public std::string_view
        {
            using Base = std::string_view;
            JSContext * ctx = nullptr;

            friend struct js_traits<std::string_view>;
            js_string(JSContext * ctx, const char * ptr, std::size_t len) : Base(ptr, len), ctx(ctx) {}
        public:

            template <typename... Args>
            js_string(Args&&... args) : Base(std::forward<Args>(args)...), ctx(nullptr) {}

            js_string(const js_string& other) = delete;

            ~js_string()
            {
                if(ctx)
                    JS_FreeCString(ctx, this->data());
            }
        };

        template <>
        struct js_traits<std::string_view>
        {
            static js_string unwrap(JSContext * ctx, JSValueConst v)
            {
                int plen;
                const char * ptr = JS_ToCStringLen(ctx, &plen, v, 0);
                if(!ptr)
                    throw exception{};
                return js_string{ctx, ptr, (std::size_t)plen};
            }

            static JSValue wrap(JSContext * ctx, std::string_view str) noexcept
            {
                return JS_NewStringLen(ctx, str.data(), (int)str.size());
            }
        };

        template <> // slower
        struct js_traits<std::string>
        {
            static std::string unwrap(JSContext * ctx, JSValueConst v)
            {
                auto str_view = js_traits<std::string_view>::unwrap(ctx, v);
                return std::string{str_view.data(), str_view.size()};
            }

            static JSValue wrap(JSContext * ctx, const std::string& str) noexcept
            {
                return JS_NewStringLen(ctx, str.data(), (int) str.size());
            }
        };

        // unwrap and free value
        template <typename T>
        T unwrap_free(JSContext * ctx, JSValue val)
        {
            if constexpr(std::is_same_v<T, void>)
            {
                JS_FreeValue(ctx, val);
                return js_traits<T>::unwrap(ctx, val);
            }
            else
            {
                try
                {
                    T result = js_traits<std::decay_t<T>>::unwrap(ctx, val);
                    JS_FreeValue(ctx, val);
                    return result;
                }
                catch(...)
                {
                    JS_FreeValue(ctx, val);
                    throw;
                }
            }
        }

        template <class Tuple, std::size_t... I>
        Tuple unwrap_args_impl(JSContext * ctx, JSValueConst * argv, std::index_sequence<I...>)
        {
            return Tuple{js_traits<std::decay_t<std::tuple_element_t<I, Tuple>>>::unwrap(ctx, argv[I])...};
        }

        template <typename... Args>
        std::tuple<Args...> unwrap_args(JSContext * ctx, JSValueConst * argv)
        {
            return unwrap_args_impl<std::tuple<Args...>>(ctx, argv, std::make_index_sequence<sizeof...(Args)>());
        }

        template <typename R, typename... Args, typename Callable>
        JSValue wrap_call(JSContext * ctx, Callable&& f, JSValueConst * argv) noexcept
        {
            try
            {
                if constexpr(std::is_same_v<R, void>)
                {
                    std::apply(std::forward<Callable>(f), unwrap_args<Args...>(ctx, argv));
                    return JS_UNDEFINED;
                } else
                {
                    return js_traits<std::decay_t<R>>::wrap(ctx, std::apply(std::forward<Callable>(f), unwrap_args<Args...>(ctx, argv)));
                }
            }
            catch(exception)
            {
                return JS_EXCEPTION;
            }
        }


        template <class Tuple, std::size_t... I>
        void wrap_args_impl(JSContext * ctx, JSValue * argv, Tuple tuple, std::index_sequence<I...>)
        {
            ((argv[I] = js_traits<std::decay_t<std::tuple_element_t<I, Tuple>>>::wrap(ctx, std::get<I>(tuple))), ...);
        }

        template <typename... Args>
        void wrap_args(JSContext * ctx, JSValue * argv, Args&&... args)
        {
            wrap_args_impl(ctx, argv, std::make_tuple(std::forward<Args>(args)...), std::make_index_sequence<sizeof...(Args)>());
        }
        // free function

        template <auto F>
        struct fwrapper
        {
            const char * name = nullptr;
        };

        template <typename R, typename... Args, R (* F)(Args...)>
        struct js_traits<fwrapper<F>>
        {
            static JSValue wrap(JSContext * ctx, fwrapper<F> fw)
            {
                return JS_NewCFunction(ctx, [](JSContext * ctx, JSValueConst this_value, int argc,
                                               JSValueConst * argv) noexcept -> JSValue {
                    return wrap_call<R, Args...>(ctx, F, argv);
                }, fw.name, sizeof...(Args));

            }
        };

        // class member function

        template <typename R, class T, typename... Args, R (T::*F)(Args...)>
        struct js_traits<fwrapper<F>>
        {
            static JSValue wrap(JSContext * ctx, fwrapper<F> fw)
            {
                return JS_NewCFunction(ctx, [](JSContext * ctx, JSValueConst this_value, int argc,
                                               JSValueConst * argv) noexcept -> JSValue {
                    //if(argc < sizeof...(Args))
                    //return JS_EXCEPTION;
                    try
                    {
                        const auto& this_sp = js_traits<std::shared_ptr<T>>::unwrap(ctx, this_value);

                        if constexpr (std::is_same_v<R, void>)
                        {
                            std::apply(F, std::tuple_cat(std::make_tuple(this_sp.get()),
                                                         detail::unwrap_args<Args...>(ctx, argv)));
                            return JS_UNDEFINED;
                        } else
                            return js_traits<std::decay_t<R>>::wrap(ctx,
                                                      std::apply(F, std::tuple_cat(std::make_tuple(this_sp.get()),
                                                                                   detail::unwrap_args<Args...>(ctx,
                                                                                                                argv)))
                            );
                    } catch(detail::exception)
                    {
                        return JS_EXCEPTION;
                    }
                }, fw.name, sizeof...(Args));

            }
        };


        // class constructor

        template <class T, typename... Args>
        struct ctor_wrapper
        {
            static_assert(std::is_constructible<T, Args...>::value, "no such constructor!");
            const char * name = nullptr;
        };

        template <class T, typename... Args>
        struct js_traits<ctor_wrapper<T, Args...>>
        {
            static JSValue wrap(JSContext * ctx, ctor_wrapper<T, Args...> cw)
            {
                return JS_NewCFunction2(ctx, [](JSContext * ctx, JSValueConst this_value, int argc,
                                                JSValueConst * argv) noexcept -> JSValue {
                    return wrap_call<std::shared_ptr<T>, Args...>(ctx, std::make_shared<T, Args...>, argv);
                }, cw.name, sizeof...(Args), JS_CFUNC_constructor, 0);

            }
        };


        // SP<T> class

        template <class T>
        struct js_traits<std::shared_ptr<T>>
        {
            inline static JSClassID QJSClassId;

            static void register_class(JSContext * ctx, const char * name, JSValue proto)
            {
                JSClassDef def{
                        name,
                        // destructor
                        [](JSRuntime * rt, JSValue obj) noexcept {
                            auto pptr = reinterpret_cast<std::shared_ptr<T> *>(JS_GetOpaque(obj, QJSClassId));
                            assert(pptr);
                            //delete pptr;
                            auto alloc = allocator<std::shared_ptr<T>>{rt};
                            using atraits = std::allocator_traits<decltype(alloc)>;
                            atraits::destroy(alloc, pptr);
                            atraits::deallocate(alloc, pptr, 1);
                        }
                };
                int e = JS_NewClass(JS_GetRuntime(ctx), JS_NewClassID(&QJSClassId), &def);
                if(e < 0)
                    throw exception{};
                JS_SetClassProto(ctx, QJSClassId, proto);
            }

            static JSValue wrap(JSContext * ctx, std::shared_ptr<T> ptr)
            {
                if(QJSClassId == 0) // not registered
                    throw exception{};
                auto jsobj = JS_NewObjectClass(ctx, QJSClassId);
                if(JS_IsException(jsobj))
                {
                    return jsobj;
                }
                //auto pptr = new std::shared_ptr<T>(std::move(ptr));
                auto alloc = allocator<std::shared_ptr<T>>{ctx};
                using atraits = std::allocator_traits<decltype(alloc)>;
                auto pptr = atraits::allocate(alloc, 1);
                atraits::construct(alloc, pptr, std::move(ptr));

                JS_SetOpaque(jsobj, pptr);
                return jsobj;
            }

            static const std::shared_ptr<T>& unwrap(JSContext * ctx, JSValueConst v)
            {
                auto ptr = reinterpret_cast<std::shared_ptr<T> *>(JS_GetOpaque2(ctx, v, QJSClassId));
                if(!ptr)
                    throw exception{};
                return *ptr;
            }
        };



        struct function // std::function replacement
        {
            JSValue (*invoker)(function * self, JSContext * ctx, JSValueConst this_value, int argc, JSValueConst * argv) = nullptr;
            void (*destroyer)(function * self) = nullptr;
            alignas(std::max_align_t) char functor[];

            template <typename Functor>
            static function * create(JSRuntime * rt, Functor&& f)
            {
                auto fptr = reinterpret_cast<function *>(js_malloc_rt(rt, sizeof(function) + sizeof(Functor)));
                if(!fptr)
                    throw std::bad_alloc{};
                new (fptr) function;
                auto functorptr = reinterpret_cast<Functor *>(fptr->functor);
                new(functorptr) Functor(std::forward<Functor>(f));
                fptr->destroyer = nullptr;
                if constexpr(!std::is_trivially_destructible_v<Functor>)
                {
                    fptr->destroyer = [](function * fptr) {
                        auto functorptr = reinterpret_cast<Functor *>(fptr->functor);
                        functorptr->~Functor();
                    };
                }
                return fptr;
            }
        };
        static_assert(std::is_trivially_destructible_v<function>);


        template <>
        struct js_traits<function>
        {
            inline static JSClassID QJSClassId;
            static void register_class(JSContext * ctx, const char * name)
            {
                JSClassDef def{
                        name,
                        // destructor
                        [](JSRuntime * rt, JSValue obj) noexcept {
                            auto fptr = reinterpret_cast<function *>(JS_GetOpaque(obj, QJSClassId));
                            assert(fptr);
                            if(fptr->destroyer)
                                fptr->destroyer(fptr);
                            js_free_rt(rt, fptr);
                        },
                        nullptr, // mark
                        // call
                        [](JSContext *ctx, JSValueConst func_obj, JSValueConst this_val, int argc, JSValueConst *argv) -> JSValue {
                            auto ptr = reinterpret_cast<function *>(JS_GetOpaque2(ctx, func_obj, QJSClassId));
                            if(!ptr)
                                return JS_EXCEPTION;
                            return ptr->invoker(ptr, ctx, this_val, argc, argv);
                        }
                };
                int e = JS_NewClass(JS_GetRuntime(ctx), JS_NewClassID(&QJSClassId), &def);
                if(e < 0)
                    throw exception{};
            }
        };


        //          properties

        template <typename Key>
        struct js_property_traits
        {
            // static void set_property(JSContext * ctx, JSValue this_obj, R key, JSValue value)
            // static JSValue get_property(JSContext * ctx, JSValue this_obj, R key)
        };

        template <>
        struct js_property_traits<const char *>
        {
            static void set_property(JSContext * ctx, JSValue this_obj, const char * name, JSValue value)
            {
                int err = JS_SetPropertyStr(ctx, this_obj, name, value);
                if(err < 0)
                    throw exception{};
            }

            static JSValue get_property(JSContext * ctx, JSValue this_obj, const char * name) noexcept
            {
                return JS_GetPropertyStr(ctx, this_obj, name);
            }
        };

        template <>
        struct js_property_traits<uint32_t>
        {
            static void set_property(JSContext * ctx, JSValue this_obj, uint32_t idx, JSValue value)
            {
                int err = JS_SetPropertyUint32(ctx, this_obj, idx, value);
                if(err < 0)
                    throw exception{};
            }

            static JSValue get_property(JSContext * ctx, JSValue this_obj, uint32_t idx) noexcept
            {
                return JS_GetPropertyUint32(ctx, this_obj, idx);
            }
        };

        template <typename Key>
        struct property_proxy
        {
            JSContext * ctx;
            JSValue this_obj;
            Key key;

            template <typename Value>
            operator Value() const
            {
                return unwrap_free<Value>(ctx, js_property_traits<Key>::get_property(ctx, this_obj, key));
            }

            template <typename Value>
            property_proxy& operator =(Value value)
            {
                js_property_traits<Key>::set_property(ctx, this_obj, key,
                                                      js_traits<Value>::wrap(ctx, std::move(value)));
                return *this;
            }
        };

    }

    class Value
    {
    public:
        JSValue v;
        JSContext * ctx = nullptr;

    public:
        template <typename T>
        Value(JSContext * ctx, T val) : ctx(ctx)
        {
            v = detail::js_traits<T>::wrap(ctx, val);
        }

        Value(const Value& rhs)
        {
            ctx = rhs.ctx;
            v = JS_DupValue(ctx, rhs.v);
        }

        Value(Value&& rhs)
        {
            std::swap(ctx, rhs.ctx);
            v = rhs.v;
        }

        ~Value()
        {
            if(ctx) JS_FreeValue(ctx, v);
        }


        template <typename T>
        T cast() const
        {
            return detail::js_traits<T>::unwrap(ctx, v);
        }

        JSValue release() // dont call freevalue
        {
            ctx = nullptr;
            return v;
        }

        operator JSValue()&&
        {
            return release();
        }


        // access properties
        template <typename Key>
        detail::property_proxy<Key> operator [](Key key)
        {
            return {ctx, v, std::move(key)};
        }


        template <auto F>
        Value& add(const char * name)
        {
            (*this)[name] = detail::fwrapper<F>{name};
            return *this;
        }

        template <typename R, typename... Args, typename Function>
        //std::enable_if_t<std::is_invocable_r<R, Function, Args...>::value, Value&>
                Value& add(const char * name, Function&& f)
        {
            (*this)[name] = detail::js_traits<std::function<R (Args...)>>::wrap(ctx, std::forward<Function>(f));
            return *this;
        }

    };


    class Runtime
    {
    public:
        JSRuntime * rt;

        Runtime()
        {
            rt = JS_NewRuntime();
            if(!rt)
                throw detail::exception{};
        }

        Runtime(const Runtime&) = delete;

        ~Runtime()
        {
            JS_FreeRuntime(rt);
        }
    };


    class Context
    {
    public:
        JSContext * ctx;

        class Module
        {
            friend class Context;

            JSModuleDef * m;
            JSContext * ctx;
            const char * name;

            using nvp = std::pair<const char *, JSValue>;
            std::vector<nvp, allocator<nvp>> exports;
        public:
            Module(JSContext * ctx, const char * name) : ctx(ctx), name(name), exports(JS_GetRuntime(ctx))
            {
                m = JS_NewCModule(ctx, name, [](JSContext * ctx, JSModuleDef * m) noexcept {
                    auto context = reinterpret_cast<Context *>(JS_GetContextOpaque(ctx));
                    if(!context)
                        return -1;
                    auto it = std::find_if(context->modules.cbegin(), context->modules.cend(),
                                           [m](const Module& module) { return module.m == m; });
                    if(it == context->modules.end())
                        return -1;
                    for(const auto& e : it->exports)
                    {
                        if(JS_SetModuleExport(ctx, m, e.first, e.second) != 0)
                            return -1;
                    }
                    return 0;
                });
                if(!m)
                    throw detail::exception{};
            }

            Module& add(const char * name, JSValue value)
            {
                exports.push_back({name, value});
                JS_AddModuleExport(ctx, m, name);
                return *this;
            }

            Module& add(const char * name, Value value)
            {
                return add(name, value.release());
            }

            template <typename T>
            Module& add(const char * name, T value)
            {
                return add(name, detail::js_traits<T>::wrap(ctx, std::move(value)));
            }

            //Module(const Module&) = delete;
        };

        std::vector<Module, allocator<Module>> modules;
    private:
        void init()
        {
            JS_SetContextOpaque(ctx, this);
            detail::js_traits<detail::function>::register_class(ctx, "C++ function");
        }
    public:
        Context(Runtime& rt) : Context(rt.rt)
        {}

        Context(JSRuntime * rt) : modules{rt}
        {
            ctx = JS_NewContext(rt);
            if(!ctx)
                throw detail::exception{};
            init();
        }

        Context(JSContext * ctx) : ctx{ctx}, modules{ctx}
        {
            init();
        }

        Context(const Context&) = delete;

        ~Context()
        {
            JS_FreeContext(ctx);
        }

        Module& addModule(const char * name)
        {
            modules.emplace_back(ctx, name);
            return modules.back();
        }

        Value global()
        {
            return Value{ctx, JS_GetGlobalObject(ctx)};
        }

        Value newObject()
        {
            return Value{ctx, JS_NewObject(ctx)};
        }

        template <class T>
        void registerClass(const char * name, JSValue proto)
        {
            detail::js_traits<std::shared_ptr<T>>::register_class(ctx, name, proto);
        }

        Value eval(std::string_view buffer, const char * filename = "<eval>", unsigned eval_flags = 0)
        {
            JSValue v = JS_Eval(ctx, buffer.data(), buffer.size(), filename, eval_flags);
            //if(JS_IsException(v))
            //throw detail::exception{};
            return Value{ctx, v};
        }

        Value evalFile(const char * filename, unsigned eval_flags = 0)
        {
            size_t buf_len;
            auto deleter = [this](void * p) { js_free(ctx, p); };
            auto buf = std::unique_ptr<uint8_t, decltype(deleter)>{js_load_file(ctx, &buf_len, filename), deleter};
            if(!buf)
                throw std::runtime_error{std::string{"evalFile: can't read file: "} + filename};
            return eval({reinterpret_cast<char *>(buf.get()), buf_len}, filename, eval_flags);
        }

    };


    namespace detail {

        template <typename R, typename... Args>
        struct js_traits<std::function<R (Args...)>>
        {
            static std::function<R (Args...)> unwrap(JSContext * ctx, JSValueConst fun_obj)
            {
                return [jsfun_obj = Value{ctx, JS_DupValue(ctx, fun_obj)}](Args&&... args) -> R {
                    const int argc = sizeof...(Args);
                    JSValue argv[argc];
                    wrap_args(jsfun_obj.ctx, argv, std::forward<Args>(args)...);
                    JSValue result = JS_Call(jsfun_obj.ctx, jsfun_obj.v, JS_UNDEFINED, argc, const_cast<JSValueConst*>(argv));
                    for(int i = 0; i < argc; i++) JS_FreeValue(jsfun_obj.ctx, argv[i]);
                    return unwrap_free<R>(jsfun_obj.ctx, result);
                };
            }

            template <typename Functor>
            static JSValue wrap(JSContext * ctx, Functor&& functor)
            {
                auto obj = JS_NewObjectClass(ctx, js_traits<function>::QJSClassId);
                if(JS_IsException(obj))
                    return JS_EXCEPTION;
                auto fptr = function::create(JS_GetRuntime(ctx), std::forward<Functor>(functor));
                fptr->invoker = [](function * self, JSContext * ctx, JSValueConst this_value, int argc, JSValueConst * argv) {
                    assert(self);
                    auto f = reinterpret_cast<Functor *>(&self->functor);
                    return wrap_call<R, Args...>(ctx, *f, argv);
                };
                JS_SetOpaque(obj, fptr);
                return obj;
            }
        };

    }

}