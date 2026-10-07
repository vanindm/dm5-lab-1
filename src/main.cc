#include <iostream>
#include <vector>
#include <map>
#include <memory>
#include <optional>

// -----------------------------------------
// Parsings
// -----------------------------------------

// Tokenizer -------------------------------

enum class TokenKind { Var, Const, Not, And, Or, Imp, Eqv, LParen, RParen, End };
constexpr std::string_view TokenKindToString(TokenKind tok) {
    switch (tok) {
        case TokenKind::Var:
            return "Var";
        case TokenKind::Const:
            return "Const";
        case TokenKind::Not:
            return "Not";
        case TokenKind::And:
            return "AND";
        case TokenKind::Or:
            return "OR";
        case TokenKind::Imp:
            return "IMPL";
        case TokenKind::Eqv:
            return "EQUIV";
        case TokenKind::LParen:
            return "LPAREN";
        case TokenKind::RParen:
            return "RPAREN";
        case TokenKind::End:
            return "END";
        default:
            return "Unknown";
    }
}

class Token {
    TokenKind kind;
    std::string text;
    size_t pos;
public:
    Token(const TokenKind& kind, std::string text, size_t pos) : kind(kind), text(std::move(text)), pos(pos) {}
    std::string ToString() const {return text;}
    size_t GetPos() const {return pos;}
    TokenKind GetKind() const {return kind;}
};
std::ostream& operator<<(std::ostream& os, const Token& tok) {
    os << tok.ToString();
    return os;
}

struct ParseError : std::runtime_error {
    size_t pos;
    ParseError(const std::string& msg, size_t p) : std::runtime_error(msg), pos(p) {}
};

std::vector<Token> Tokenize(const std::string& s) {
    static const std::vector<std::pair<std::string, TokenKind>> ops = {
        {"<->", TokenKind::Eqv}, {"->", TokenKind::Imp},
        {"&", TokenKind::And}, {"|", TokenKind::Or},
        {"~", TokenKind::Not},
        {"(", TokenKind::LParen}, {")", TokenKind::RParen},
    };

    std::vector<Token> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = s[i];
        if (std::isspace(c)) { ++i; continue; }

        if (std::isalpha(c) || c == '_') {
            size_t start = i;
            while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) ++i;
            out.push_back({TokenKind::Var, s.substr(start, i - start), start});
            continue;
        }
        if (c == '0' || c == '1') {
            out.push_back({TokenKind::Const, std::string(1, c), i});
            ++i;
            continue;
        }

        bool matched = false;
        for (const auto& [text, kind] : ops) {
            if (s.compare(i, text.size(), text) == 0) {
                out.push_back({kind, text, i});
                i += text.size();
                matched = true;
                break;
            }
        }
        if (!matched)
            throw ParseError("неизвестный символ '" + std::string(1, s[i]) + "'", i);
    }
    out.push_back({TokenKind::End, "", s.size()});
    return out;
}

// Helpers ---------------------------------
std::string describe(const Token& t) {
    return t.GetKind() == TokenKind::End ? "неожиданный конец формулы"
                              : "неожиданный символ '" + t.ToString() + "'";
}

// AST ------------------------------------------------

enum class NodeKind { Var, Const, Not, And, Or, Imp, Eqv };

class Node {
    NodeKind kind;
    std::string name;
    bool value = false;
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;
    Node() {}
    bool Equal(const Node& b) const {
        if (kind != b.kind) return false;
        switch (kind) {
            case NodeKind::Var:   return name == b.name;
            case NodeKind::Const: return value == b.value;
            case NodeKind::Not:   return left->Equal(*b.left);
            default:              return left->Equal(*b.left) && right->Equal(*b.right);
        }
    }
public:
    Node(const Node* node) : kind(node->kind), name(node->name), value(node->value) {
        if (node->left)
            left = std::make_unique<Node>(node->left.get());
        if (node->right)
            right = std::make_unique<Node>(node->right.get());
    }
    NodeKind GetKind() const {return kind;}
    std::string ToString() const {
        switch(kind) {
            case NodeKind::Var:
            case NodeKind::Const:
                return name;
            case NodeKind::Eqv:
                return "("+left->ToString() + "<->" +right->ToString()+")";
            case NodeKind::Imp:
                return "("+left->ToString() + "->" + right->ToString()+")";
            case NodeKind::Not:
                return "~"+left->ToString();
            case NodeKind::Or:
                return "("+left->ToString() + "|" + right->ToString()+")";
            case NodeKind::And:
                return "("+left->ToString() + "&" + right->ToString()+")";
        }
        return name;
    }
    const Node* GetLeft() const {return left.get();}
    const Node* GetRight() const {return right.get();}
    bool HasValue() const {return value;}
    static std::unique_ptr<Node> Var(std::string n) {
        auto p = std::unique_ptr<Node>(new Node());
        p->kind = NodeKind::Var;
        p->name = std::move(n);
        return p;
    }
    static std::unique_ptr<Node> Const(bool v) {
        auto p = std::unique_ptr<Node>(new Node());
        p->kind = NodeKind::Const;
        p->value = v;
        return p;
    }
    static std::unique_ptr<Node> Unary(NodeKind k, std::unique_ptr<Node> a) {
        auto p = std::unique_ptr<Node>(new Node());
        p->kind = k;
        p->left = std::move(a);
        return p;
    }
    static std::unique_ptr<Node> Binary(NodeKind k, std::unique_ptr<Node> a,
                                        std::unique_ptr<Node> b) {
        auto p = std::unique_ptr<Node>(new Node());
        p->kind = k;
        p->left = std::move(a);
        p->right = std::move(b);
        return p;
    }
    bool operator==(const Node& b) const {
        return Equal(b);
    }
    bool operator!=(const Node& b) const {
        return !Equal(b);
    }
};

// LL(1) Parser -----------------------------------------------------

enum class Symbol {
    Var, Const, Not, And, Or, Imp, Eqv, LParen, RParen, End,
    E, E1, I, I1, D, D1, C, C1, N, A,
    MkEqv, MkImp, MkOr, MkAnd, MkNot
};

bool isTerminal(Symbol s) { return s <= Symbol::End; }
bool isAction(Symbol s) { return s >= Symbol::MkEqv; }
NodeKind kindOf(Symbol action) {
    switch (action) {
        case Symbol::MkEqv: return NodeKind::Eqv;
        case Symbol::MkImp: return NodeKind::Imp;
        case Symbol::MkOr:  return NodeKind::Or;
        default:         return NodeKind::And;
    }
}

std::map<std::pair<Symbol, TokenKind>, std::vector<Symbol>> BuildTable() {
    std::map<std::pair<Symbol, TokenKind>, std::vector<Symbol>> M;
    for (TokenKind t : {TokenKind::Not, TokenKind::Var, TokenKind::Const, TokenKind::LParen}) {
        M[{Symbol::E, t}] = {Symbol::I, Symbol::E1};
        M[{Symbol::I, t}] = {Symbol::D, Symbol::I1};
        M[{Symbol::D, t}] = {Symbol::C, Symbol::D1};
        M[{Symbol::C, t}] = {Symbol::N, Symbol::C1};
    }
    M[{Symbol::E1, TokenKind::Eqv}] = {Symbol::Eqv, Symbol::I, Symbol::MkEqv, Symbol::E1};
    for (TokenKind t : {TokenKind::RParen, TokenKind::End}) M[{Symbol::E1, t}] = {};

    M[{Symbol::I1, TokenKind::Imp}] = {Symbol::Imp, Symbol::I, Symbol::MkImp};
    for (TokenKind t : {TokenKind::Eqv, TokenKind::RParen, TokenKind::End}) M[{Symbol::I1, t}] = {};

    M[{Symbol::D1, TokenKind::Or}] = {Symbol::Or, Symbol::C, Symbol::MkOr, Symbol::D1};
    for (TokenKind t : {TokenKind::Imp, TokenKind::Eqv, TokenKind::RParen, TokenKind::End}) M[{Symbol::D1, t}] = {};

    M[{Symbol::C1, TokenKind::And}] = {Symbol::And, Symbol::N, Symbol::MkAnd, Symbol::C1};
    for (TokenKind t : {TokenKind::Or, TokenKind::Imp, TokenKind::Eqv, TokenKind::RParen, TokenKind::End}) M[{Symbol::C1, t}] = {};

    M[{Symbol::N, TokenKind::Not}] = {Symbol::Not, Symbol::N, Symbol::MkNot};
    for (TokenKind t : {TokenKind::Var, TokenKind::Const, TokenKind::LParen}) M[{Symbol::N, t}] = {Symbol::A};

    M[{Symbol::A, TokenKind::Var}] = {Symbol::Var};
    M[{Symbol::A, TokenKind::Const}] = {Symbol::Const};
    M[{Symbol::A, TokenKind::LParen}] = {Symbol::LParen, Symbol::E, Symbol::RParen};
    return M;
}

class Parser {
    std::map<std::pair<Symbol, TokenKind>, std::vector<Symbol>> M;
public:
    Parser() : M(BuildTable()) {}
    std::unique_ptr<Node> Parse(const std::vector<Token>& toks) const {
        std::vector<Symbol> stack = {Symbol::End, Symbol::E};
        std::vector<std::unique_ptr<Node>> values;
        size_t pos = 0;
    
        auto pop = [](std::vector<std::unique_ptr<Node>>& values) {
            auto n = std::move(values.back());
            values.pop_back();
            return n;
        };
    
        while (true) {
            Symbol X = stack.back();
            stack.pop_back();
            const Token& tok = toks[pos];
    
            if (isTerminal(X)) {
                if (X != static_cast<Symbol>(tok.GetKind())) {
                    if (X == Symbol::RParen) throw ParseError("ожидалась ')'", tok.GetPos());
                    if (X == Symbol::End) throw ParseError("лишний символ '" + tok.ToString() + "'", tok.GetPos());
                    throw ParseError(describe(tok), tok.GetPos());
                }
                if (tok.GetKind() == TokenKind::Var) values.push_back(Node::Var(tok.ToString()));
                if (tok.GetKind() == TokenKind::Const) values.push_back(Node::Const(tok.ToString() == "1"));
                if (X == Symbol::End) break;
                ++pos;
            } else if (isAction(X)) {
                if (X == Symbol::MkNot) {
                    values.push_back(Node::Unary(NodeKind::Not, pop(values)));
                } else {
                    auto right = pop(values);
                    auto left = pop(values);
                    values.push_back(Node::Binary(kindOf(X), std::move(left), std::move(right)));
                }
            } else {
                auto it = M.find({X, tok.GetKind()});
                if (it == M.end()) {
                    bool nullable = X == Symbol::E1 || X == Symbol::I1 || X == Symbol::D1 || X == Symbol::C1;
                    if (nullable) throw ParseError("лишний символ '" + tok.ToString() + "'", tok.GetPos());
                    throw ParseError(describe(tok), tok.GetPos());
                }
                const std::vector<Symbol>& rhs = it->second;
                for (auto r = rhs.rbegin(); r != rhs.rend(); ++r) stack.push_back(*r);
            }
        }
        return pop(values);
    }
};
static Parser defaultParser = Parser();
std::unique_ptr<Node> ParseStatement(const std::string& a) {
    return defaultParser.Parse(Tokenize(a));
}

// -----------------------------------------
// Logic

class CorrectFormula {
    std::unique_ptr<Node> pattern;
    static bool MatchNode(const Node* p, const Node* f, std::map<std::string, const Node*>& s) {
        if (p->GetKind() == NodeKind::Var) {
                auto [it, inserted] = s.emplace(p->ToString(), f);
            return inserted || *it->second == *f;
        }
        if (p->GetKind() != f->GetKind()) return false;
        if (p->GetKind() == NodeKind::Const) return p->HasValue() == f->HasValue();
        if (!MatchNode(p->GetLeft(), f->GetLeft(), s)) return false;
        return !p->GetRight() || MatchNode(p->GetRight(), f->GetRight(), s);
    }
public:
    CorrectFormula(const Node* _pattern) {
        pattern = std::make_unique<Node>(_pattern);
    }
    const Node& GetPattern() const {
        return *pattern;
    }
    std::optional<std::map<std::string, const Node*>> Match(const Node& f) const {
        std::map<std::string, const Node*> s;
        if (MatchNode(pattern.get(), &f, s)) return s;
        return std::nullopt;
    }
};

class InferenceRule {
public:
    virtual ~InferenceRule() = default;
    virtual std::string ToString() const = 0;
    virtual std::optional<std::vector<size_t>> Derive(const Node& conclusion, const std::vector<std::unique_ptr<Node>>& proven) const = 0;
};

class MP : public InferenceRule {
public:
    std::string ToString() const override { return "MP"; }
    std::optional<std::vector<size_t>> Derive(const Node& b, const std::vector<std::unique_ptr<Node>>& proven) const override {
        for (size_t j = 0; j < proven.size(); ++j) {
            const Node* imp = proven[j].get();
            if (imp->GetKind() != NodeKind::Imp || *imp->GetRight() != b) continue;
            for (size_t i = 0; i < proven.size(); ++i)
                if (*proven[i] == *imp->GetLeft()) return std::vector<size_t>{i, j};
        }
        return std::nullopt;
    }
};

// -----------------------------------------
// Verifier
// -----------------------------------------

enum class Rule {
    Nil, proven, ax, MP, beta
};

struct ProverOutput {
    Rule rule;
    union {
        size_t axNum;
        size_t formulaNum;
        std::pair<const Node*, const Node*> mpVal;
    };
    std::map<std::string, const Node*> subst;
};

class System {
    std::vector<CorrectFormula> ax;
    std::vector<std::unique_ptr<Node>> proven;
    Parser parser;
    MP ruleMP;
public:
    System(Parser parser) : proven(0), parser(parser) {};

    /* 
     * @param statement формула B
     * @retval возвращает структуру ProverOutput
     */
    ProverOutput ProveStatement(const Node* statement) {
        for (auto it = proven.begin(); it != proven.end(); ++it) {
            if (*(*it) == *statement) {
                return {Rule::proven, {0}};
            }
        }
        for (auto it = ax.begin(); it != ax.end(); ++it) {
            if (auto out = (*it).Match(*statement)) {
                proven.push_back(std::make_unique<Node>(statement));
                return {Rule::ax, {.axNum = static_cast<size_t>(it - ax.begin())}, out.value()};
            }
        }
        for (auto it = proven.begin(); it != proven.end(); ++it) {
            if (auto out = CorrectFormula((*it).get()).Match(*statement)) {
                size_t idx = static_cast<size_t>(it - proven.begin());
                proven.push_back(std::make_unique<Node>(statement));
                return {Rule::beta, {.formulaNum = idx}, out.value()};
            }
        }
        std::optional<std::vector<size_t>> source = ruleMP.Derive(*statement, proven);
        if (source.has_value()) {
            proven.push_back(std::make_unique<Node>(statement));
            return {Rule::MP, {.mpVal = std::pair<const Node*, const Node*>{proven[source.value()[0]].get(), proven[source.value()[1]].get()}}};
        }
        return {Rule::Nil, {0}};
    }
    const Node& GetAxiom(size_t i) const {
        return ax[i].GetPattern();
    }
    const Node& GetFormula(size_t i) const {
        return *(proven[i]);
    }
    static System Var1(const Parser& parser) {
        System system(parser);
        system.proven.push_back(parser.Parse(Tokenize("p -> (q -> p)")));
        system.proven.push_back(parser.Parse(Tokenize("(s -> (p -> q)) -> ((s -> p) -> (s -> q))")));
        system.proven.push_back(parser.Parse(Tokenize("(((p->f))->f) -> p")));
        for (auto &x : system.proven) {
            system.ax.push_back(CorrectFormula(x.get()));
        }
        return system;
    }
    static System Var4(const Parser& parser) {
        System system(parser);
        system.proven.push_back(parser.Parse(Tokenize("p -> (q -> p)")));
        system.proven.push_back(parser.Parse(Tokenize("(s -> (p -> q)) -> ((s -> p) -> (s -> q))")));
        system.proven.push_back(parser.Parse(Tokenize("(p & q) -> p")));
        system.proven.push_back(parser.Parse(Tokenize("(p & q) -> q")));
        system.proven.push_back(parser.Parse(Tokenize("p -> (q -> (p & q))")));
        system.proven.push_back(parser.Parse(Tokenize("p -> (p | q)")));
        system.proven.push_back(parser.Parse(Tokenize("q -> (p | q)")));
        system.proven.push_back(parser.Parse(Tokenize("(p -> r) -> ((q -> r) -> ((p | q) -> r))")));
        system.proven.push_back(parser.Parse(Tokenize("~p -> (p -> q)")));
        system.proven.push_back(parser.Parse(Tokenize("(p -> q) -> ((p -> ~q) -> ~p)")));
        system.proven.push_back(parser.Parse(Tokenize("p | ~p")));
        for (auto &x : system.proven) {
            system.ax.push_back(CorrectFormula(x.get()));
        }
        return system;
    }
};

// -----------------------------------------
// REPL and main
// -----------------------------------------

void REPL() {
    std::string in;
    int running = true;
    System system = System::Var1(defaultParser);
    while (running) {
        std::cout << ">>> ";
        if (!std::getline(std::cin, in, '\n')) {
            running = false;
        }
        if (in == ""){
            continue;
        }
        try {
            std::unique_ptr<Node> parsedStatement = ParseStatement(in);
            ProverOutput out = system.ProveStatement(parsedStatement.get());
            switch(out.rule) {
                case Rule::ax:
                    std::cout << "Формула выводима подстановкой ";
                    for (auto it = out.subst.begin(); it != out.subst.end(); ++it) {
                        std::cout << it->second->ToString() << " в " << it->first << ", ";
                    }
                    std::cout << "в аксиому " << system.GetAxiom(out.axNum).ToString() << "\n";
                    break;
                case Rule::beta:
                    std::cout <<"Формула выводима из формулы " << system.GetFormula(out.formulaNum).ToString() << " по правилу beta с подстановкой ";
                    for (auto it = out.subst.begin(); it != out.subst.end(); ++it) {
                        std::cout << it->second->ToString() << " в " << it->first << ", ";
                    }
                    std::cout << "в формулу " << system.GetFormula(out.formulaNum).ToString() << "\n";
                    break;
                case Rule::proven: 
                    std::cout << "Формула была доказана ранее или является аксиомой.\n";
                    break;
                case Rule::Nil:
                    std::cout << "Формула не выводима. \n";
                    break;
                case Rule::MP:
                    std::cout << "Формула выводима из формул " << out.mpVal.first->ToString() << " и " << out.mpVal.second->ToString() << " по правилу MP.\n";
            }
        } catch (const ParseError& e) {
            std::cout << "Ошибка в формуле:" << e.what() << "\n";
        }
    }
}

int main(int argc, char* argv[]) {
    REPL();
    return 0;
}
