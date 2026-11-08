#include <vector>  
#include <string>

class Base {  
public:  
  virtual void method() = 0;
  ~Base() {}
};   

class Derived : public Base {  
public:  
  virtual void method() {};
  virtual ~Derived() {} 
};

class MyType {
public:
    int id;
    std::string name;
    MyType(int i, const std::string& n) : id(i), name(n) {}
};

void foo() {  
  std::vector<int> v1;  
  for (const auto x1 : v1) {
  }  
  std::vector<MyType> vec = {{1, "obj1"}, {2, "obj2"}, {3, "obj3"}};
  for (const auto x12 : vec) { 
  }
}  