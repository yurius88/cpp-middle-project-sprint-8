#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>

namespace fs = std::filesystem;

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "../tests/tests_data"
#endif

#ifndef REFACTOR_TOOL
#define REFACTOR_TOOL "./refactor_tool"
#endif

namespace {

struct ExecResult {
    int exitCode = -1;
    std::string output;
};

// Запускает команду через shell, забирает stdout+stderr, возвращает код возврата.
ExecResult runCapture(const std::string &Cmd) {
    ExecResult Result;
    FILE *Pipe = popen((Cmd + " 2>&1").c_str(), "r");
    if (!Pipe) {
        return Result;
    }
    std::array<char, 4096> Buffer;
    size_t N = 0;
    while ((N = fread(Buffer.data(), 1, Buffer.size(), Pipe)) > 0) {
        Result.output.append(Buffer.data(), N);
    }
    const int Status = pclose(Pipe);
    Result.exitCode = (Status != -1 && WIFEXITED(Status)) ? WEXITSTATUS(Status) : -1;
    return Result;
}

// Находит подходящий компилятор в системе.
std::string findCompiler() {
    for (const char *Candidate : {"clang++", "clang++-20", "g++-15", "g++"}) {
        if (runCapture(std::string("command -v ") + Candidate).exitCode == 0) {
            return Candidate;
        }
    }
    return {};
}

std::string readFile(const fs::path &Path) {
    std::ifstream In(Path);
    std::ostringstream Buf;
    Buf << In.rdbuf();
    return Buf.str();
}

void writeFile(const fs::path &Path, const std::string &Content) {
    std::ofstream Out(Path);
    Out << Content;
}

// Возвращает копию исходника с первой заменой From -> To. Сообщает об ошибке, если не нашлось.
std::string replaceFirst(const std::string &Source, const std::string &From, const std::string &To) {
    const size_t Pos = Source.find(From);
    if (Pos == std::string::npos) {
        return {};
    }
    std::string Copy = Source;
    Copy.replace(Pos, From.size(), To);
    return Copy;
}

}  // namespace

// Тест AddressSanitizer
TEST(AsanLeak, FixedByVirtualDtor) {
    const std::string Src = readFile(fs::path(TEST_DATA_DIR) / "leak_example.cpp");
    const std::string BeforeSrc = replaceFirst(Src, "virtual ~Base", "~Base");
    ASSERT_FALSE(BeforeSrc.empty()) << "В leak_example.cpp ожидается строка 'virtual ~Base'";

    const fs::path Dir = fs::temp_directory_path() / "refactor_tool_asan_test";
    fs::remove_all(Dir);
    fs::create_directories(Dir);
    const fs::path BeforeCpp = Dir / "leak_before.cpp";
    const fs::path AfterCpp = Dir / "leak_after.cpp";
    writeFile(BeforeCpp, BeforeSrc);
    writeFile(AfterCpp, BeforeSrc);

    // После рефакторинга добавляется virtual.
    const int ToolRc = std::system((std::string(REFACTOR_TOOL) + " \"" + AfterCpp.string() + "\" --").c_str());
    ASSERT_EQ(ToolRc, 0);
    const std::string AfterCode = readFile(AfterCpp);
    ASSERT_NE(AfterCode.find("virtual ~Base"), std::string::npos);

    const std::string CXX = findCompiler();
    ASSERT_FALSE(CXX.empty()) << "Не найден ни один компилятор";

    const auto CompileCmd = [&](const fs::path &SrcFile, const fs::path &Exe) {
        return "LC_ALL=C " + CXX + " -std=c++17 -g -fsanitize=address \"" + SrcFile.string() + "\" -o \"" +
               Exe.string() + "\"";
    };
    const fs::path BeforeExe = Dir / "leak_before";
    const fs::path AfterExe = Dir / "leak_after";
    ASSERT_EQ(std::system(CompileCmd(BeforeCpp, BeforeExe).c_str()), 0);
    ASSERT_EQ(std::system(CompileCmd(AfterCpp, AfterExe).c_str()), 0);

    // ДО: деструктор не виртуальный — LeakSanitizer должен сообщить об утечке.
    const ExecResult Before =
        runCapture("ASAN_OPTIONS=detect_leaks=1:new_delete_type_mismatch=0 \"" + BeforeExe.string() + "\"");
    EXPECT_NE(Before.exitCode, 0) << Before.output;
    EXPECT_NE(Before.output.find("LeakSanitizer"), std::string::npos) << Before.output;
    EXPECT_NE(Before.output.find("4000 byte(s) leaked"), std::string::npos) << Before.output;

    // ПОСЛЕ: деструктор виртуальный — утечки нет.
    const ExecResult After = runCapture("\"" + AfterExe.string() + "\"");
    EXPECT_EQ(After.exitCode, 0) << After.output;
    EXPECT_EQ(After.output.find("LeakSanitizer"), std::string::npos) << After.output;
}

// Тест perf
TEST(PerfRangeFor, ReferenceVersionNotSlower) {
    const std::string Src = readFile(fs::path(TEST_DATA_DIR) / "perf_example.cpp");
    // perf_example.cpp содержит копирование: "for (const auto obj : vec)".
    ASSERT_NE(Src.find("const auto obj"), std::string::npos)
        << "В perf_example.cpp ожидается 'for (const auto obj : vec)'";

    const fs::path Dir = fs::temp_directory_path() / "refactor_tool_perf_test";
    fs::remove_all(Dir);
    fs::create_directories(Dir);
    const fs::path BeforeCpp = Dir / "perf_before.cpp";
    const fs::path AfterCpp = Dir / "perf_after.cpp";
    writeFile(BeforeCpp, Src);
    writeFile(AfterCpp, Src);

    // После рефакторинга добавляется '&'.
    const int ToolRc = std::system((std::string(REFACTOR_TOOL) + " \"" + AfterCpp.string() + "\" --").c_str());
    ASSERT_EQ(ToolRc, 0);
    const std::string AfterCode = readFile(AfterCpp);
    ASSERT_NE(AfterCode.find("const auto& obj"), std::string::npos) << AfterCode;

    const std::string CXX = findCompiler();
    ASSERT_FALSE(CXX.empty()) << "Не найден ни один компилятор";

    const fs::path BeforeExe = Dir / "perf_before";
    const fs::path AfterExe = Dir / "perf_after";
    auto compile = [&CXX](const fs::path &SrcFile, const fs::path &Exe) {
        return std::system(
            ("LC_ALL=C " + CXX + " -O2 -std=c++17 \"" + SrcFile.string() + "\" -o \"" + Exe.string() + "\"").c_str());
    };
    ASSERT_EQ(compile(BeforeCpp, BeforeExe), 0);
    ASSERT_EQ(compile(AfterCpp, AfterExe), 0);

    const ExecResult PerfCheck = runCapture("perf stat -e task-clock true");
    const ExecResult ValgrindCheck = runCapture("command -v valgrind");

    if (PerfCheck.exitCode == 0) {
        // Замер CPU-времени через perf stat с повторениями; парсим значение task-clock.
        auto measure = [](const fs::path &Exe) {
            const ExecResult Res = runCapture("LC_ALL=C perf stat -r 5 -x, -e task-clock \"" + Exe.string() + "\"");
            const size_t Pos = Res.output.find("task-clock");
            const size_t LineStart = Res.output.rfind('\n', Pos);
            const std::string Line = Res.output.substr(
                LineStart == std::string::npos ? 0 : LineStart + 1,
                Pos == std::string::npos ? std::string::npos
                                         : Pos - (LineStart == std::string::npos ? 0 : LineStart + 1));
            std::stringstream Ss(Line);
            std::string ValueStr, Unit;
            std::getline(Ss, ValueStr, ',');
            std::getline(Ss, Unit, ',');
            double Value = std::stod(ValueStr);
            if (Unit == "msec")
                Value /= 1e3;
            else if (Unit == "usec")
                Value /= 1e6;
            else if (Unit == "nsec")
                Value /= 1e9;
            return Value;  // секунды
        };
        const double BeforeSec = measure(BeforeExe);
        const double AfterSec = measure(AfterExe);
        std::cerr << "perf task-clock: до=" << BeforeSec << "с, после=" << AfterSec << "с\n";
        EXPECT_LT(AfterSec, BeforeSec);
    } else if (ValgrindCheck.exitCode == 0) {
        // perf недоступен (типично для WSL2): считаем инструкции через callgrind.
        auto instructionCount = [&Dir](const fs::path &Exe, const char *OutFile) {
            fs::path CgOut = Dir / OutFile;
            const ExecResult Res = runCapture("valgrind --tool=callgrind --callgrind-out-file=\"" + CgOut.string() +
                                              "\" \"" + Exe.string() + "\"");
            const size_t Marker = Res.output.find("I   refs:");
            EXPECT_NE(Marker, std::string::npos) << Res.output;
            std::string Number;
            for (size_t I = Marker + 8; I < Res.output.size(); ++I) {
                const char C = Res.output[I];
                if (std::isdigit(static_cast<unsigned char>(C)) || C == ',') {
                    Number += C;
                } else if (!Number.empty()) {
                    break;
                }
            }
            Number.erase(std::remove(Number.begin(), Number.end(), ','), Number.end());
            return std::stoll(Number);
        };
        const long long BeforeIr = instructionCount(BeforeExe, "before.out");
        const long long AfterIr = instructionCount(AfterExe, "after.out");
        std::cerr << "callgrind Ir: до=" << BeforeIr << ", после=" << AfterIr << " (экономия "
                  << 100.0 * (BeforeIr - AfterIr) / BeforeIr << "%)\n";
        EXPECT_LT(AfterIr, BeforeIr);
    } else {
        GTEST_SKIP() << "perf недоступен, а valgrind не найден — замер пропущен";
    }
}
