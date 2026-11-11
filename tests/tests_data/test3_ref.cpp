#include <vector>
#include <string>
#include <iostream>

struct CustomType {
    int id;
    std::string name;
};

void process() {
    std::vector<CustomType> vec = {{1, "a"}, {2, "b"}};

    // const auto без &
    for (const auto& x : vec) {
        std::cout << x.id;
    }

    // const явный тип без &
    for (const CustomType& x : vec) {
        std::cout << x.id;
    }

    // const decltype без &
    for (const decltype(vec)::value_type& x : vec) {
        std::cout << x.id;
    }

    // Фундаментальный тип, не меняется
    std::vector<int> ints = {1, 2};
    for (const int x : ints) {
        std::cout << x;
    }

    // Уже с &
    for (const auto& x : vec) {
        std::cout << x.id;
    }
}