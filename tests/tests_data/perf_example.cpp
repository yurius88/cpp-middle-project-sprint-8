#include <vector>
#include <string>

struct HeavyObject {
    std::string data[1000];  // Тяжёлый объект для имитации копирования
};

int main() {
    std::vector<HeavyObject> vec(100000);  // Большой контейнер
    for (const auto obj : vec) {  // Копирование без &
        const auto& data = obj.data[0];
    }
    return 0;
}