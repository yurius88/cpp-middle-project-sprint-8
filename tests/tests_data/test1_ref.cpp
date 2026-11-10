#include <iostream>

class BaseNoDtor {
public:
    int x;
    virtual ~BaseNoDtor() = default;
};

class DerivedFromNoDtor : public BaseNoDtor {};

class BaseNonVirtual {  // Невиртуальный деструктор
public:
    virtual ~BaseNonVirtual() {} //Убедимся что один раз virtual
};

class DerivedFromNonVirtual : public BaseNonVirtual {};

class DerivedFromNonVirtual1 : public DerivedFromNonVirtual {};

class BaseVirtual {  // Уже виртуальный деструктор, не меняется
public:
    virtual ~BaseVirtual() {}
};

class DerivedFromVirtual : public BaseVirtual {};

class Standalone {  // Нет наследников, не меняется
public:
    ~Standalone() {}
};