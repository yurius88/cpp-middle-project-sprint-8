#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/Support/CommandLine.h"

#include <cctype>
#include <unordered_set>

#include "RefactorTool.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolCategory("refactor-tool options");

// Метод run вызывается для каждого совпадения с матчем.
// Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
void RefactorHandler::run(const MatchFinder::MatchResult &Result) {
    auto &Diag = Result.Context->getDiagnostics();
    auto &SM = *Result.SourceManager;  // Получаем SourceManager для проверки isInMainFile

    if (const auto *Dtor = Result.Nodes.getNodeAs<CXXDestructorDecl>("nonVirtualDtor")) {
        handle_nv_dtor(Dtor, Diag, SM);
    }

    if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("missingOverride")) {
        handle_miss_override(Method, Diag, SM);
    }

    if (const auto *LoopVar = Result.Nodes.getNodeAs<VarDecl>("loopVar")) {
        handle_crange_for(LoopVar, Diag, SM);
    }
}

// Случай 1: невиртуальный десктруктор
void RefactorHandler::handle_nv_dtor(const CXXDestructorDecl *Dtor, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!Dtor->getLocation().isValid() || !SM.isInMainFile(Dtor->getLocation())) {
        return;
    }

    const unsigned Key = makeLocationKey(SM, Dtor->getLocation());
    if (!processedLocations.insert(Key).second) {
        return;
    }

    SourceLocation InsertLoc = Dtor->getLocation();
    Rewrite.InsertTextBefore(InsertLoc, "virtual ");

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Объявлен деструктор");
    Diag.Report(InsertLoc, DiagID);
}

// Случай 2: отсутствие override.
void RefactorHandler::handle_miss_override(const CXXMethodDecl *Method, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!Method->getLocation().isValid() || !SM.isInMainFile(Method->getLocation())) {
        return;
    }

    SourceLocation RParen;
    if (const TypeSourceInfo *TSI = Method->getTypeSourceInfo()) {
        FunctionProtoTypeLoc ProtoLoc = TSI->getTypeLoc().getAs<FunctionProtoTypeLoc>();
        if (!ProtoLoc.isNull()) {
            RParen = ProtoLoc.getRParenLoc();
        }
    }
    if (RParen.isInvalid()) {
        const FileID FID = SM.getFileID(Method->getLocation());
        const unsigned NameOff = SM.getFileOffset(Method->getNameInfo().getLoc());
        const StringRef Buf = SM.getBufferData(FID);
        int Depth = 0;
        for (unsigned I = NameOff; I < Buf.size(); ++I) {
            if (Buf[I] == '(') {
                ++Depth;
            } else if (Buf[I] == ')') {
                if (--Depth == 0) {
                    RParen = SM.getLocForStartOfFile(FID).getLocWithOffset(I);
                    break;
                }
            }
        }
    }
    if (RParen.isInvalid()) {
        return;
    }
    SourceLocation AfterParen = RParen.getLocWithOffset(1);

    const unsigned Key = makeLocationKey(SM, AfterParen);
    if (!processedLocations.insert(Key).second) {
        return;
    }

    Rewrite.InsertTextBefore(AfterParen, " override");

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Объявлен метод");
    Diag.Report(AfterParen, DiagID);
}

// Случай 3: отсутствие & в range-for
void RefactorHandler::handle_crange_for(const VarDecl *LoopVar, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!LoopVar->getLocation().isValid() || !SM.isInMainFile(LoopVar->getLocation())) {
        return;
    }

    SourceLocation NameLoc = LoopVar->getLocation();
    const FileID FID = SM.getFileID(NameLoc);
    const unsigned NameOff = SM.getFileOffset(NameLoc);
    const StringRef Buf = SM.getBufferData(FID);
    unsigned InsertOff = NameOff;
    while (InsertOff > 0 && std::isspace(static_cast<unsigned char>(Buf[InsertOff - 1]))) {
        --InsertOff;
    }
    SourceLocation InsertLoc = SM.getLocForStartOfFile(FID).getLocWithOffset(InsertOff);

    const unsigned Key = makeLocationKey(SM, InsertLoc);
    if (!processedLocations.insert(Key).second) {
        return;
    }

    Rewrite.InsertTextBefore(InsertLoc, "&");

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Объявлена переменная");
    Diag.Report(InsertLoc, DiagID);
}

auto NvDtorMatcher() {
    return cxxRecordDecl(isDefinition(), isExpansionInMainFile(),
                         isDerivedFrom(cxxRecordDecl(hasMethod(
                             cxxDestructorDecl(unless(isVirtual()), unless(isImplicit())).bind("nonVirtualDtor")))))
        .bind("derivedClass");
}

auto NoOverrideMatcher() {
    return cxxMethodDecl(isExpansionInMainFile(), unless(isImplicit()), unless(cxxDestructorDecl()), isOverride(),
                         unless(hasAttr(clang::attr::Override)))
        .bind("missingOverride");
}

auto NoRefConstVarInRangeLoopMatcher() {
    return cxxForRangeStmt(hasLoopVariable(varDecl(hasType(qualType(isConstQualified())),
                                                   unless(anyOf(hasType(referenceType()), hasType(builtinType()))))
                                               .bind("loopVar")))
        .bind("rangeLoop");
}

// Конструктор принимает Rewriter для изменения кода.
ComplexConsumer::ComplexConsumer(Rewriter &Rewrite) : Handler(Rewrite) {
    // Создаем MatchFinder и добавляем матчеры.
    Finder.addMatcher(NvDtorMatcher(), &Handler);
    Finder.addMatcher(NoOverrideMatcher(), &Handler);
    Finder.addMatcher(NoRefConstVarInRangeLoopMatcher(), &Handler);
}

// Метод HandleTranslationUnit вызывается для каждого файла.
void ComplexConsumer::HandleTranslationUnit(ASTContext &Context) { Finder.matchAST(Context); }

std::unique_ptr<ASTConsumer> CodeRefactorAction::CreateASTConsumer(CompilerInstance &CI, StringRef file) {
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<ComplexConsumer>(RewriterForCodeRefactor);
}

bool CodeRefactorAction::BeginSourceFileAction(CompilerInstance &CI) {
    // Инициализируем Rewriter для рефакторинга.
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return true;  // Возвращаем true, чтобы продолжить обработку файла.
}

void CodeRefactorAction::EndSourceFileAction() {
    // Применяем изменения в файле.
    if (RewriterForCodeRefactor.overwriteChangedFiles()) {
        llvm::errs() << "Error applying changes to files.\n";
    }
}

int main(int argc, const char **argv) {
    // Парсер опций: Обрабатывает флаги командной строки, компиляционные базы данных.
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    // Создаем ClangTool
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    // Запускаем RefactorAction.
    return Tool.run(newFrontendActionFactory<CodeRefactorAction>().get());
}