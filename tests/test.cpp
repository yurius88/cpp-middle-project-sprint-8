#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "../tests/tests_data"
#endif

#ifndef REFACTOR_TOOL
#define REFACTOR_TOOL "./refactor_tool"
#endif

namespace {

// Запускает refactor_tool над копией исходного файла и возвращает содержимое результата.
std::string runTool(const std::string &SourceName) {
    const fs::path DataDir = TEST_DATA_DIR;
    const fs::path Original = DataDir / SourceName;
    const fs::path Work = DataDir / ("unit_" + SourceName);

    fs::copy_file(Original, Work, fs::copy_options::overwrite_existing);

    std::string Cmd = std::string(REFACTOR_TOOL) + " \"" + Work.string() + "\" --";
    const int Rc = std::system(Cmd.c_str());
    EXPECT_EQ(Rc, 0) << "refactor_tool завершился с ошибкой для " << SourceName;

    std::ifstream In(Work);
    std::stringstream Buffer;
    Buffer << In.rdbuf();
    In.close();
    fs::remove(Work);
    return Buffer.str();
}

std::string readRef(const std::string &RefName) {
    std::ifstream In(fs::path(TEST_DATA_DIR) / RefName);
    std::stringstream Buffer;
    Buffer << In.rdbuf();
    return Buffer.str();
}

// Сравнение без учёта пробелов и пустых строк (аналог diff -w -B).
bool equivalentIgnoringWhitespace(const std::string &Left, const std::string &Right) {
    auto NextToken = [](const std::string &Text, size_t &Pos) {
        std::string Token;
        while (Pos < Text.size()) {
            const char C = Text[Pos++];
            if (std::isspace(static_cast<unsigned char>(C))) {
                if (!Token.empty())
                    return Token;
                continue;
            }
            Token += C;
        }
        return Token;
    };
    size_t LP = 0, RP = 0;
    while (true) {
        const std::string LT = NextToken(Left, LP);
        const std::string RT = NextToken(Right, RP);
        if (LT.empty() && RT.empty())
            return true;
        if (LT != RT)
            return false;
    }
}

}  // namespace

// Случай 1: невиртуальный деструктор базового класса с наследниками получает virtual.
TEST(RefactorVirtualDtor, AddsVirtualToNonVirtualDtor) {
    const std::string Result = runTool("test1.cpp");
    const std::string Expected = readRef("test1_ref.cpp");
    EXPECT_TRUE(equivalentIgnoringWhitespace(Result, Expected));

    // Точечно: virtual появился перед деструктором базового класса с наследниками.
    EXPECT_NE(Result.find("virtual ~BaseNoDtor()"), std::string::npos);
    EXPECT_NE(Result.find("virtual ~BaseNonVirtual()"), std::string::npos);
    // Уже виртуальный деструктор не дублируется.
    EXPECT_EQ(Result.find("virtual virtual"), std::string::npos);
    // Класс без наследников не изменяется.
    EXPECT_NE(Result.find("~Standalone()"), std::string::npos);
}

// Случай 2: переопределённые методы без override получают override.
TEST(RefactorOverride, AddsOverrideToOverriddenMethods) {
    const std::string Result = runTool("test2.cpp");
    const std::string Expected = readRef("test2_ref.cpp");
    EXPECT_TRUE(equivalentIgnoringWhitespace(Result, Expected));

    EXPECT_NE(Result.find("void func() override {}"), std::string::npos);
    EXPECT_NE(Result.find("void func(int a) override {}"), std::string::npos);
    // Метод, уже помеченный override, не дублируется.
    EXPECT_EQ(Result.find("override override"), std::string::npos);
}

// Случай 3: const T без ссылки в range-for получает &.
TEST(RefactorRangeFor, AddsReferenceToConstLoopVariable) {
    const std::string Result = runTool("test3.cpp");
    const std::string Expected = readRef("test3_ref.cpp");
    EXPECT_TRUE(equivalentIgnoringWhitespace(Result, Expected));

    // Инструмент вставляет '&' сразу после типа.
    EXPECT_NE(Result.find("const auto& x"), std::string::npos);
    EXPECT_NE(Result.find("const CustomType& x"), std::string::npos);
    EXPECT_NE(Result.find("decltype(vec)::value_type& x"), std::string::npos);
    // Фундаментальный тип не изменяется.
    EXPECT_NE(Result.find("const int x : ints"), std::string::npos);
}
