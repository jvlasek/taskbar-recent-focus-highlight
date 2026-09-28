#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <string>
struct Node;
struct Value { int number=0; bool unset=false; std::shared_ptr<Node> element; };
struct UIElement;
struct Object {
    std::shared_ptr<Value> value;
    explicit operator bool() const {return bool(value);}
    bool operator==(Object b) const {return value==b.value;}
    bool operator!=(Object b) const {return !(*this==b);}
    template<class T> T as() const {return T{value->element};}
};
Object Box(int n) {auto v=std::make_shared<Value>();v->number=n;return {v};}
namespace DependencyProperty {Object UnsetValue(){static auto v=[] {auto p=std::make_shared<Value>();p->unset=true;return Object{p};}();return v;}}
struct Node {Object local=DependencyProperty::UnsetValue();};
struct UIElement {
    std::shared_ptr<Node> node;
    Object ReadLocalValue(int) const {return node->local;}
    void SetValue(int,Object v) {node->local=v;}
    void ClearValue(int) {node->local=DependencyProperty::UnsetValue();}
};
namespace Controls {struct Canvas {static int ZIndexProperty(){return 0;}};}
namespace winrt {template<class T>T unbox_value(Object o){assert(o && !o.value->unset);return static_cast<T>(o.value->number);}}
struct IconZOrderBag {std::map<std::wstring,Object> values;Object Lookup(const wchar_t* key) const{return values.at(key);}};
// PRODUCTION_HELPERS
IconZOrderBag Owner(UIElement element,Object prior,int ours) {
    auto v=std::make_shared<Value>();v->element=element.node;
    return {{{L"element",{v}},{L"prior",prior},{L"ours",Box(ours)}}};
}
int main() {
    UIElement e{std::make_shared<Node>()};
    auto unset=DependencyProperty::UnsetValue();
    e.SetValue(0,Box(8));auto owner=Owner(e,unset,8);
    assert(StillOwnIconZIndex(e,owner));RestoreIconZEntry(owner);assert(e.ReadLocalValue(0)==unset);
    auto prior=Box(-17);e.SetValue(0,Box(6));owner=Owner(e,prior,6);
    RestoreIconZEntry(owner);assert(e.ReadLocalValue(0)==prior);
    e.SetValue(0,Box(6));owner=Owner(e,prior,6);auto later=Box(99);e.SetValue(0,later);
    assert(!StillOwnIconZIndex(e,owner));RestoreIconZEntry(owner);assert(e.ReadLocalValue(0)==later);
    e.ClearValue(0);RestoreIconZEntry(owner);assert(e.ReadLocalValue(0)==unset);
    std::cout<<"PASS: native ZIndex restore preserves unset, explicit prior, and later-owner values\n";
}
