#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace vscript {

struct HaxeError : std::runtime_error {
    int line;
    HaxeError(int line, const std::string& message) : std::runtime_error(message), line(line) {}
};

enum class HxK {
    Null, True, False, Int, Float, Str, Regex, Ident, This, Super, Field, Call, Index, New, Unop, Postfix, Binop, Assign, Ternary,
    ArrayLit, MapLit, ObjLit, Func, Cast, Paren, Block, Var, If, While, DoWhile, For, Return, Break, Continue, Switch, Try, Throw,
    Comprehension, Is
};

struct HxNode;
using HxPtr = std::shared_ptr<HxNode>;

struct HxStrPart {
    bool isExpr = false;
    std::string text;
    HxPtr expr;
};

struct HxCase {
    std::vector<HxPtr> values;
    HxPtr guard;
    std::vector<HxPtr> body;
    bool isDefault = false;
};

struct HxCatch {
    std::string name;
    std::vector<HxPtr> body;
};

struct HxParam {
    std::string name;
    std::string type;
    HxPtr value;
};

struct HxNode {
    HxK k = HxK::Null;
    int line = 0;
    size_t start = 0;
    size_t end = 0;
    std::string s;
    std::string type;
    double num = 0;
    bool safe = false;
    bool singleQuoted = false;
    HxPtr a;
    HxPtr b;
    HxPtr c;
    std::vector<HxPtr> list;
    std::vector<std::string> names;
    std::vector<std::string> keys;
    std::vector<HxStrPart> parts;
    std::vector<HxCase> cases;
    std::vector<HxCatch> catches;
    std::vector<HxParam> params;
};

struct HxMember {
    std::string name;
    bool isFunction = false;
    bool isStatic = false;
    bool isOverride = false;
    std::string type;
    std::vector<HxParam> params;
    HxPtr value;
    std::vector<HxPtr> body;
    int line = 0;
};

struct HxScript {
    std::vector<std::string> imports;
    bool hasClass = false;
    std::string className;
    std::string parent;
    std::vector<HxMember> members;
    std::vector<HxPtr> topLevel;
};

HxScript parseHaxe(const std::string& source);

}
