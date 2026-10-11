#include "SkinExpression.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
enum class TokenKind
{
    end, number, string, identifier, plus, minus, multiply, divide,
    equal, notEqual, less, lessEqual, greater, greaterEqual,
    leftParen, rightParen, comma
};

struct Token
{
    Token() = default;
    explicit Token (TokenKind kindIn) : kind (kindIn) {}
    Token (TokenKind kindIn, juce::String textIn, double numberIn)
        : kind (kindIn), text (std::move (textIn)), number (numberIn) {}

    TokenKind kind = TokenKind::end;
    juce::String text;
    double number = 0.0;
};

class Lexer
{
public:
    Lexer (const juce::String& sourceIn, SkinValidationReport& reportIn,
           const juce::String& fileIn, const juce::String& pointerIn)
        : source (sourceIn), report (reportIn), file (fileIn), pointer (pointerIn)
    {
    }

    std::optional<std::vector<Token>> run()
    {
        while (position < source.length())
        {
            if (juce::CharacterFunctions::isWhitespace (source[position]))
            {
                ++position;
                continue;
            }

            const auto character = source[position];
            if (juce::CharacterFunctions::isDigit (character))
            {
                if (! lexNumber()) return std::nullopt;
            }
            else if (character == '\'')
            {
                if (! lexString()) return std::nullopt;
            }
            else if (juce::CharacterFunctions::isLetter (character) || character == '_')
            {
                lexIdentifier();
            }
            else if (! lexOperator())
            {
                return std::nullopt;
            }

            if (tokens.size() > 256)
            {
                fail ("EXPR007", "Expression contains more than 256 lexical tokens.");
                return std::nullopt;
            }
        }

        tokens.push_back ({ TokenKind::end, {}, 0.0 });
        return tokens;
    }

private:
    bool lexNumber()
    {
        const auto start = position;
        while (position < source.length()
               && juce::CharacterFunctions::isDigit (source[position]))
            ++position;
        if (position < source.length() && source[position] == '.')
        {
            ++position;
            if (position >= source.length()
                || ! juce::CharacterFunctions::isDigit (source[position]))
                return fail ("EXPR001", "Number has no digits after its decimal point.");
            while (position < source.length()
                   && juce::CharacterFunctions::isDigit (source[position]))
                ++position;
        }
        if (position < source.length()
            && (source[position] == 'e' || source[position] == 'E'))
        {
            ++position;
            if (position < source.length()
                && (source[position] == '+' || source[position] == '-'))
                ++position;
            if (position >= source.length()
                || ! juce::CharacterFunctions::isDigit (source[position]))
                return fail ("EXPR001", "Number exponent is incomplete.");
            while (position < source.length()
                   && juce::CharacterFunctions::isDigit (source[position]))
                ++position;
        }

        const auto text = source.substring (start, position);
        const auto value = text.getDoubleValue();
        if (! std::isfinite (value))
            return fail ("EXPR001", "Numeric literals must be finite.");
        tokens.push_back ({ TokenKind::number, text, value });
        return true;
    }

    bool lexString()
    {
        ++position;
        juce::String value;
        while (position < source.length())
        {
            const auto character = source[position++];
            if (character == '\'')
            {
                tokens.push_back ({ TokenKind::string, value, 0.0 });
                return true;
            }
            if (character != '\\')
            {
                value += character;
                continue;
            }
            if (position >= source.length())
                return fail ("EXPR001", "String ends with an incomplete escape.");
            const auto escaped = source[position++];
            if (escaped == '\\') value += '\\';
            else if (escaped == '\'') value += '\'';
            else if (escaped == 'n') value += '\n';
            else if (escaped == 't') value += '\t';
            else return fail ("EXPR001", "String contains an unsupported escape.");
        }
        return fail ("EXPR001", "String literal is not terminated.");
    }

    void lexIdentifier()
    {
        const auto start = position;
        const auto tokenPath = source.substring (position).startsWith ("token.");
        while (position < source.length())
        {
            const auto character = source[position];
            if (! juce::CharacterFunctions::isLetterOrDigit (character)
                && character != '_' && character != '.'
                && character != '[' && character != ']'
                && (! tokenPath || character != '-'))
                break;
            ++position;
        }
        tokens.push_back ({ TokenKind::identifier, source.substring (start, position), 0.0 });
    }

    bool lexOperator()
    {
        const auto character = source[position++];
        switch (character)
        {
            case '+': tokens.emplace_back (TokenKind::plus); return true;
            case '-': tokens.emplace_back (TokenKind::minus); return true;
            case '*': tokens.emplace_back (TokenKind::multiply); return true;
            case '/': tokens.emplace_back (TokenKind::divide); return true;
            case '(': tokens.emplace_back (TokenKind::leftParen); return true;
            case ')': tokens.emplace_back (TokenKind::rightParen); return true;
            case ',': tokens.emplace_back (TokenKind::comma); return true;
            case '=':
                if (match ('=')) { tokens.emplace_back (TokenKind::equal); return true; }
                return fail ("EXPR001", "Assignment is not allowed in skin expressions.");
            case '!':
                if (match ('=')) { tokens.emplace_back (TokenKind::notEqual); return true; }
                return fail ("EXPR001", "Use the 'not' keyword for boolean negation.");
            case '<':
                tokens.emplace_back (match ('=') ? TokenKind::lessEqual : TokenKind::less);
                return true;
            case '>':
                tokens.emplace_back (match ('=') ? TokenKind::greaterEqual : TokenKind::greater);
                return true;
            default:
                return fail ("EXPR001", "Expression contains an unsupported lexical token.");
        }
    }

    bool match (juce::juce_wchar expected)
    {
        if (position < source.length() && source[position] == expected)
        {
            ++position;
            return true;
        }
        return false;
    }

    bool fail (juce::String code, juce::String message)
    {
        report.addError (std::move (code), file, pointer, std::move (message));
        return false;
    }

    const juce::String& source;
    SkinValidationReport& report;
    juce::String file, pointer;
    int position = 0;
    std::vector<Token> tokens;
};

enum class Opcode
{
    constant, context,
    negate, logicalNot,
    add, subtract, multiply, divide,
    equal, notEqual, less, lessEqual, greater, greaterEqual,
    logicalAnd, logicalOr,
    minimum, maximum, clamp, absolute, round, lerp, alpha, mix
};

struct Node
{
    Node() = default;
    Node (Opcode opcodeIn, SkinExpressionType typeIn, SkinExpressionValue constantIn)
        : opcode (opcodeIn), type (typeIn), constant (std::move (constantIn)) {}

    Opcode opcode = Opcode::constant;
    SkinExpressionType type = SkinExpressionType::number;
    SkinExpressionValue constant { 0.0 };
    juce::String path;
    std::vector<int> children;
    int depth = 1;
    bool tokenStringAsColour = false;
};

SkinExpressionType typeOf (const SkinExpressionValue& value)
{
    if (std::holds_alternative<double> (value)) return SkinExpressionType::number;
    if (std::holds_alternative<bool> (value)) return SkinExpressionType::boolean;
    if (std::holds_alternative<juce::String> (value)) return SkinExpressionType::string;
    return SkinExpressionType::colour;
}

struct ParsedProgram
{
    std::vector<Node> nodes;
    int root = -1;
};

class Parser
{
public:
    Parser (const std::vector<Token>& tokensIn,
            const std::map<juce::String, SkinExpressionType>& contextIn,
            const SkinTokenStore& tokenStoreIn,
            SkinValidationReport& reportIn,
            juce::String fileIn, juce::String pointerIn)
        : tokens (tokensIn), context (contextIn), tokenStore (tokenStoreIn),
          report (reportIn), file (std::move (fileIn)), pointer (std::move (pointerIn))
    {
    }

    std::optional<ParsedProgram> run()
    {
        const auto root = parseExpression (1);
        if (failed || root < 0)
            return std::nullopt;
        if (current().kind != TokenKind::end)
        {
            fail ("EXPR002", "Unexpected tokens follow the complete expression.");
            return std::nullopt;
        }
        return ParsedProgram { std::move (nodes), root };
    }

private:
    int parseExpression (int minimumBindingPower)
    {
        auto left = parsePrefix();
        if (left < 0)
            return -1;

        for (;;)
        {
            const auto binding = bindingPower (current());
            if (binding < minimumBindingPower)
                break;
            const auto operation = current();
            ++position;
            const auto right = parseExpression (binding + 1);
            if (right < 0)
                return -1;
            left = makeBinary (operation, left, right);
            if (left < 0)
                return -1;
        }
        return left;
    }

    int parsePrefix()
    {
        const auto token = current();
        if (token.kind == TokenKind::number)
        {
            ++position;
            return addNode ({ Opcode::constant, SkinExpressionType::number,
                              SkinExpressionValue (token.number) });
        }
        if (token.kind == TokenKind::string)
        {
            ++position;
            return addNode ({ Opcode::constant, SkinExpressionType::string,
                              SkinExpressionValue (token.text) });
        }
        if (token.kind == TokenKind::minus)
        {
            ++position;
            const auto operand = parseExpression (6);
            if (operand < 0) return -1;
            if (nodes[static_cast<size_t> (operand)].type != SkinExpressionType::number)
                return typeFailure ("Unary minus requires a number.");
            return addOperation (Opcode::negate, SkinExpressionType::number, { operand });
        }
        if (token.kind == TokenKind::leftParen)
        {
            ++position;
            if (! enterNesting()) return -1;
            const auto expression = parseExpression (1);
            if (current().kind != TokenKind::rightParen)
                return syntaxFailure ("Parenthesised expression is not closed.");
            ++position;
            --nesting;
            return expression;
        }
        if (token.kind != TokenKind::identifier)
            return syntaxFailure ("Expected a literal, reference, function or parenthesis.");

        ++position;
        if (token.text == "true" || token.text == "false")
            return addNode ({ Opcode::constant, SkinExpressionType::boolean,
                              SkinExpressionValue (token.text == "true") });
        if (token.text == "not")
        {
            const auto operand = parseExpression (6);
            if (operand < 0) return -1;
            if (nodes[static_cast<size_t> (operand)].type != SkinExpressionType::boolean)
                return typeFailure ("'not' requires a boolean.");
            return addOperation (Opcode::logicalNot, SkinExpressionType::boolean, { operand });
        }
        if (current().kind == TokenKind::leftParen)
            return parseFunction (token.text);
        if (token.text.startsWith ("token."))
            return parseTokenConstant (token.text.substring (6));

        const auto found = context.find (token.text);
        if (found == context.end())
        {
            fail ("EXPR003", "Context path is not allowed for this renderer: " + token.text + ".");
            return -1;
        }
        Node node;
        node.opcode = Opcode::context;
        node.type = found->second;
        node.path = token.text;
        return addNode (std::move (node));
    }

    int parseTokenConstant (const juce::String& path)
    {
        Node node;
        node.opcode = Opcode::constant;
        node.path = path;
        if (const auto* numberValue = tokenStore.find<double> (path))
        {
            node.type = SkinExpressionType::number;
            node.constant = *numberValue;
        }
        else if (const auto* durationValue = tokenStore.find<int64_t> (path))
        {
            node.type = SkinExpressionType::number;
            node.constant = static_cast<double> (*durationValue);
        }
        else if (const auto* stringValue = tokenStore.find<juce::String> (path))
        {
            node.type = SkinExpressionType::string;
            node.constant = *stringValue;
        }
        else if (const auto* colourValue = tokenStore.find<juce::Colour> (path))
        {
            node.type = SkinExpressionType::colour;
            node.constant = *colourValue;
        }
        else if (tokenStore.contains (path))
        {
            fail ("EXPR006", "Token type cannot be used in an expression: " + path + ".");
            return -1;
        }
        else
        {
            fail ("EXPR003", "Token path does not exist: " + path + ".");
            return -1;
        }
        return addNode (std::move (node));
    }

    int parseFunction (const juce::String& name)
    {
        const auto known = name == "min" || name == "max" || name == "clamp"
                        || name == "abs" || name == "round" || name == "lerp"
                        || name == "alpha" || name == "mix" || name == "color";
        if (! known)
        {
            fail ("EXPR004", "Unknown expression function: " + name + ".");
            return -1;
        }

        ++position;
        if (! enterNesting()) return -1;
        std::vector<int> arguments;
        if (current().kind != TokenKind::rightParen)
        {
            for (;;)
            {
                const auto argument = parseExpression (1);
                if (argument < 0) return -1;
                arguments.push_back (argument);
                if (current().kind != TokenKind::comma)
                    break;
                ++position;
            }
        }
        if (current().kind != TokenKind::rightParen)
            return syntaxFailure ("Function argument list is not closed.");
        ++position;
        --nesting;

        const auto requireCount = [&] (size_t count)
        {
            if (arguments.size() == count) return true;
            fail ("EXPR005", name + "() received the wrong number of arguments.");
            return false;
        };
        const auto requireNumbers = [&]
        {
            const auto valid = std::all_of (arguments.begin(), arguments.end(), [&] (int index)
            {
                return nodes[static_cast<size_t> (index)].type == SkinExpressionType::number;
            });
            if (! valid) fail ("EXPR006", name + "() accepts numbers only.");
            return valid;
        };

        if (name == "color")
        {
            if (! requireCount (1)) return -1;
            const auto& argument = nodes[static_cast<size_t> (arguments[0])];
            if (argument.opcode != Opcode::constant
                || argument.type != SkinExpressionType::string)
                return typeFailure ("color() requires one literal string.");
            const auto colour = parseSkinCssColour (std::get<juce::String> (argument.constant));
            if (! colour)
                return typeFailure ("color() requires #RRGGBB or #RRGGBBAA.");
            Node colourNode { Opcode::constant, SkinExpressionType::colour,
                              SkinExpressionValue (*colour) };
            colourNode.path = argument.path;
            colourNode.tokenStringAsColour = argument.path.isNotEmpty();
            return addNode (std::move (colourNode));
        }
        if (name == "min" || name == "max")
        {
            if (arguments.empty())
            {
                fail ("EXPR005", name + "() requires at least one argument.");
                return -1;
            }
            if (! requireNumbers()) return -1;
            return addOperation (name == "min" ? Opcode::minimum : Opcode::maximum,
                                 SkinExpressionType::number, std::move (arguments));
        }
        if (name == "clamp" || name == "lerp")
        {
            if (! requireCount (3)) return -1;
            if (! requireNumbers()) return -1;
            return addOperation (name == "clamp" ? Opcode::clamp : Opcode::lerp,
                                 SkinExpressionType::number, std::move (arguments));
        }
        if (name == "abs" || name == "round")
        {
            if (! requireCount (1)) return -1;
            if (! requireNumbers()) return -1;
            return addOperation (name == "abs" ? Opcode::absolute : Opcode::round,
                                 SkinExpressionType::number, std::move (arguments));
        }
        if (name == "alpha")
        {
            if (! requireCount (2)) return -1;
            if (nodes[static_cast<size_t> (arguments[0])].type != SkinExpressionType::colour
                || nodes[static_cast<size_t> (arguments[1])].type != SkinExpressionType::number)
                return typeFailure ("alpha() requires (color, number).");
            return addOperation (Opcode::alpha, SkinExpressionType::colour,
                                 std::move (arguments));
        }
        if (! requireCount (3)) return -1;
        if (nodes[static_cast<size_t> (arguments[0])].type != SkinExpressionType::colour
            || nodes[static_cast<size_t> (arguments[1])].type != SkinExpressionType::colour
            || nodes[static_cast<size_t> (arguments[2])].type != SkinExpressionType::number)
            return typeFailure ("mix() requires (color, color, number).");
        return addOperation (Opcode::mix, SkinExpressionType::colour, std::move (arguments));
    }

    int makeBinary (const Token& operation, int left, int right)
    {
        const auto leftType = nodes[static_cast<size_t> (left)].type;
        const auto rightType = nodes[static_cast<size_t> (right)].type;
        Opcode opcode = Opcode::add;
        SkinExpressionType result = SkinExpressionType::number;
        switch (operation.kind)
        {
            case TokenKind::plus: opcode = Opcode::add; break;
            case TokenKind::minus: opcode = Opcode::subtract; break;
            case TokenKind::multiply: opcode = Opcode::multiply; break;
            case TokenKind::divide: opcode = Opcode::divide; break;
            case TokenKind::less: opcode = Opcode::less; result = SkinExpressionType::boolean; break;
            case TokenKind::lessEqual: opcode = Opcode::lessEqual; result = SkinExpressionType::boolean; break;
            case TokenKind::greater: opcode = Opcode::greater; result = SkinExpressionType::boolean; break;
            case TokenKind::greaterEqual: opcode = Opcode::greaterEqual; result = SkinExpressionType::boolean; break;
            case TokenKind::equal: opcode = Opcode::equal; result = SkinExpressionType::boolean; break;
            case TokenKind::notEqual: opcode = Opcode::notEqual; result = SkinExpressionType::boolean; break;
            case TokenKind::identifier:
                opcode = operation.text == "and" ? Opcode::logicalAnd : Opcode::logicalOr;
                result = SkinExpressionType::boolean;
                break;
            case TokenKind::end:
            case TokenKind::number:
            case TokenKind::string:
            case TokenKind::leftParen:
            case TokenKind::rightParen:
            case TokenKind::comma:
                return syntaxFailure ("Unsupported binary operator.");
        }

        if (opcode == Opcode::equal || opcode == Opcode::notEqual)
        {
            if (leftType != rightType)
                return typeFailure ("Equality operands must have exactly the same type.");
        }
        else if (opcode == Opcode::logicalAnd || opcode == Opcode::logicalOr)
        {
            if (leftType != SkinExpressionType::boolean || rightType != SkinExpressionType::boolean)
                return typeFailure ("and/or operands must be boolean.");
        }
        else if (leftType != SkinExpressionType::number || rightType != SkinExpressionType::number)
        {
            return typeFailure ("Arithmetic and ordered comparisons require numbers.");
        }
        return addOperation (opcode, result, { left, right });
    }

    int addOperation (Opcode opcode, SkinExpressionType type, std::vector<int> children)
    {
        Node node;
        node.opcode = opcode;
        node.type = type;
        node.children = std::move (children);
        return addNode (std::move (node));
    }

    int addNode (Node node)
    {
        for (const auto child : node.children)
            node.depth = std::max (node.depth, nodes[static_cast<size_t> (child)].depth + 1);
        if (node.depth > 32)
        {
            fail ("EXPR008", "Expression syntax tree exceeds depth 32.");
            return -1;
        }
        nodes.push_back (std::move (node));
        return static_cast<int> (nodes.size() - 1);
    }

    bool enterNesting()
    {
        if (++nesting <= 32)
            return true;
        fail ("EXPR008", "Expression nesting exceeds depth 32.");
        return false;
    }

    int bindingPower (const Token& token) const
    {
        if (token.kind == TokenKind::identifier && token.text == "or") return 1;
        if (token.kind == TokenKind::identifier && token.text == "and") return 2;
        if (token.kind == TokenKind::equal || token.kind == TokenKind::notEqual
            || token.kind == TokenKind::less || token.kind == TokenKind::lessEqual
            || token.kind == TokenKind::greater || token.kind == TokenKind::greaterEqual) return 3;
        if (token.kind == TokenKind::plus || token.kind == TokenKind::minus) return 4;
        if (token.kind == TokenKind::multiply || token.kind == TokenKind::divide) return 5;
        return 0;
    }

    int typeFailure (const juce::String& message)
    {
        fail ("EXPR006", message);
        return -1;
    }

    int syntaxFailure (const juce::String& message)
    {
        fail ("EXPR002", message);
        return -1;
    }

    void fail (juce::String code, juce::String message)
    {
        if (failed) return;
        failed = true;
        report.addError (std::move (code), file, pointer, std::move (message));
    }

    const Token& current() const { return tokens[static_cast<size_t> (position)]; }

    const std::vector<Token>& tokens;
    const std::map<juce::String, SkinExpressionType>& context;
    const SkinTokenStore& tokenStore;
    SkinValidationReport& report;
    juce::String file, pointer;
    std::vector<Node> nodes;
    int position = 0;
    int nesting = 0;
    bool failed = false;
};

float srgbToLinear (float value)
{
    return value <= 0.04045f ? value / 12.92f
                             : std::pow ((value + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb (float value)
{
    return value <= 0.0031308f ? value * 12.92f
                               : 1.055f * std::pow (value, 1.0f / 2.4f) - 0.055f;
}

juce::Colour mixColours (juce::Colour left, juce::Colour right, double amount)
{
    const auto t = static_cast<float> (juce::jlimit (0.0, 1.0, amount));
    const auto leftAlpha = left.getFloatAlpha();
    const auto rightAlpha = right.getFloatAlpha();
    const auto alpha = leftAlpha + (rightAlpha - leftAlpha) * t;
    const auto channel = [&] (float leftChannel, float rightChannel)
    {
        const auto premultipliedLeft = srgbToLinear (leftChannel) * leftAlpha;
        const auto premultipliedRight = srgbToLinear (rightChannel) * rightAlpha;
        const auto mixed = premultipliedLeft + (premultipliedRight - premultipliedLeft) * t;
        return alpha > 0.0f ? linearToSrgb (mixed / alpha) : 0.0f;
    };
    return juce::Colour::fromFloatRGBA (channel (left.getFloatRed(), right.getFloatRed()),
                                         channel (left.getFloatGreen(), right.getFloatGreen()),
                                         channel (left.getFloatBlue(), right.getFloatBlue()),
                                         alpha);
}
} // namespace

struct SkinExpressionProgram
{
    std::vector<Node> nodes;
    int root = -1;
    juce::String file, pointer;
};

namespace
{
class Evaluator
{
public:
    Evaluator (const SkinExpressionProgram& programIn,
               const SkinExpressionContext& contextIn,
               SkinValidationReport* reportIn)
        : program (programIn), context (contextIn), report (reportIn)
    {
    }

    std::optional<SkinExpressionValue> run()
    {
        return evaluate (program.root);
    }

private:
    std::optional<SkinExpressionValue> evaluate (int index)
    {
        const auto& node = program.nodes[static_cast<size_t> (index)];
        if (node.opcode == Opcode::constant)
            return node.constant;
        if (node.opcode == Opcode::context)
        {
            if (! context.lookup)
                return runtimeFailure ("RUNTIME001", "Expression context lookup is unavailable.");
            const auto utf8 = node.path.toUTF8();
            const auto value = context.lookup ({ utf8.getAddress(),
                                                  static_cast<size_t> (utf8.sizeInBytes() - 1) });
            if (! value || typeOf (*value) != node.type)
                return runtimeFailure ("RUNTIME001",
                                       "Expression context value is missing or has the wrong type.");
            if (const auto* number = std::get_if<double> (&*value); number != nullptr
                && ! std::isfinite (*number))
                return nonFinite();
            return value;
        }

        if (node.opcode == Opcode::logicalAnd || node.opcode == Opcode::logicalOr)
        {
            const auto left = evaluate (node.children[0]);
            if (! left) return std::nullopt;
            const auto boolean = std::get<bool> (*left);
            if ((node.opcode == Opcode::logicalAnd && ! boolean)
                || (node.opcode == Opcode::logicalOr && boolean))
                return SkinExpressionValue (boolean);
            return evaluate (node.children[1]);
        }

        if (node.opcode == Opcode::negate || node.opcode == Opcode::logicalNot)
        {
            const auto operand = evaluate (node.children[0]);
            if (! operand) return std::nullopt;
            if (node.opcode == Opcode::logicalNot)
                return SkinExpressionValue (! std::get<bool> (*operand));
            return checkedNumber (-std::get<double> (*operand));
        }

        if (node.opcode == Opcode::minimum || node.opcode == Opcode::maximum)
        {
            const auto first = evaluate (node.children[0]);
            if (! first) return std::nullopt;
            auto result = std::get<double> (*first);
            for (size_t i = 1; i < node.children.size(); ++i)
            {
                const auto value = evaluate (node.children[i]);
                if (! value) return std::nullopt;
                result = node.opcode == Opcode::minimum
                    ? std::min (result, std::get<double> (*value))
                    : std::max (result, std::get<double> (*value));
            }
            return checkedNumber (result);
        }

        if (node.opcode == Opcode::absolute || node.opcode == Opcode::round)
        {
            const auto value = evaluate (node.children[0]);
            if (! value) return std::nullopt;
            return checkedNumber (node.opcode == Opcode::absolute
                                      ? std::abs (std::get<double> (*value))
                                      : std::round (std::get<double> (*value)));
        }

        if (node.opcode == Opcode::clamp || node.opcode == Opcode::lerp)
        {
            const auto first = evaluate (node.children[0]);
            const auto second = evaluate (node.children[1]);
            const auto third = evaluate (node.children[2]);
            if (! first || ! second || ! third) return std::nullopt;
            const auto a = std::get<double> (*first);
            const auto b = std::get<double> (*second);
            const auto c = std::get<double> (*third);
            const auto lower = std::min (b, c);
            const auto upper = std::max (b, c);
            return checkedNumber (node.opcode == Opcode::clamp
                                      ? juce::jlimit (lower, upper, a)
                                      : a + (b - a) * c);
        }

        if (node.opcode == Opcode::alpha || node.opcode == Opcode::mix)
        {
            const auto first = evaluate (node.children[0]);
            const auto second = evaluate (node.children[1]);
            if (! first || ! second) return std::nullopt;
            if (node.opcode == Opcode::alpha)
                return SkinExpressionValue (
                    std::get<juce::Colour> (*first).withAlpha (
                        static_cast<float> (juce::jlimit (0.0, 1.0,
                                                         std::get<double> (*second)))));
            const auto third = evaluate (node.children[2]);
            if (! third) return std::nullopt;
            return SkinExpressionValue (mixColours (std::get<juce::Colour> (*first),
                                                     std::get<juce::Colour> (*second),
                                                     std::get<double> (*third)));
        }

        const auto left = evaluate (node.children[0]);
        const auto right = evaluate (node.children[1]);
        if (! left || ! right) return std::nullopt;
        switch (node.opcode)
        {
            case Opcode::add:
                return checkedNumber (std::get<double> (*left) + std::get<double> (*right));
            case Opcode::subtract:
                return checkedNumber (std::get<double> (*left) - std::get<double> (*right));
            case Opcode::multiply:
                return checkedNumber (std::get<double> (*left) * std::get<double> (*right));
            case Opcode::divide:
                if (std::get<double> (*right) == 0.0)
                {
                    if (report != nullptr && ! report->hasCode ("EXPR021"))
                        report->addWarning ("EXPR021", program.file, program.pointer,
                                            "Division by zero returned number 0.");
                    return SkinExpressionValue (0.0);
                }
                return checkedNumber (std::get<double> (*left) / std::get<double> (*right));
            case Opcode::equal: return SkinExpressionValue (*left == *right);
            case Opcode::notEqual: return SkinExpressionValue (*left != *right);
            case Opcode::less:
                return SkinExpressionValue (std::get<double> (*left) < std::get<double> (*right));
            case Opcode::lessEqual:
                return SkinExpressionValue (std::get<double> (*left) <= std::get<double> (*right));
            case Opcode::greater:
                return SkinExpressionValue (std::get<double> (*left) > std::get<double> (*right));
            case Opcode::greaterEqual:
                return SkinExpressionValue (std::get<double> (*left) >= std::get<double> (*right));
            case Opcode::constant:
            case Opcode::context:
            case Opcode::negate:
            case Opcode::logicalNot:
            case Opcode::logicalAnd:
            case Opcode::logicalOr:
            case Opcode::minimum:
            case Opcode::maximum:
            case Opcode::clamp:
            case Opcode::absolute:
            case Opcode::round:
            case Opcode::lerp:
            case Opcode::alpha:
            case Opcode::mix:
                break;
        }
        return std::nullopt;
    }

    std::optional<SkinExpressionValue> checkedNumber (double value)
    {
        return std::isfinite (value) ? std::optional<SkinExpressionValue> (value)
                                     : nonFinite();
    }

    std::optional<SkinExpressionValue> nonFinite()
    {
        if (report != nullptr && ! report->hasCode ("EXPR020"))
            report->addError ("EXPR020", program.file, program.pointer,
                              "Expression produced a non-finite number.");
        return std::nullopt;
    }

    std::optional<SkinExpressionValue> runtimeFailure (juce::String code,
                                                       juce::String message)
    {
        if (report != nullptr && ! report->hasCode (code))
            report->addError (std::move (code), program.file, program.pointer,
                              std::move (message));
        return std::nullopt;
    }

    const SkinExpressionProgram& program;
    const SkinExpressionContext& context;
    SkinValidationReport* report;
};
} // namespace

SkinExpression::SkinExpression (std::shared_ptr<const SkinExpressionProgram> programIn)
    : program (std::move (programIn))
{
}

std::optional<SkinExpression> SkinExpression::compile (
    juce::String source,
    const std::map<juce::String, SkinExpressionType>& allowedContext,
    const SkinTokenStore& tokens,
    SkinValidationReport& report,
    juce::String file,
    juce::String pointer)
{
    const auto lexical = Lexer (source, report, file, pointer).run();
    if (! lexical)
        return std::nullopt;
    auto parsed = Parser (*lexical, allowedContext, tokens, report, file, pointer).run();
    if (! parsed)
        return std::nullopt;
    auto program = std::make_shared<SkinExpressionProgram>();
    program->nodes = std::move (parsed->nodes);
    program->root = parsed->root;
    program->file = std::move (file);
    program->pointer = std::move (pointer);
    return SkinExpression (std::move (program));
}

SkinExpressionType SkinExpression::resultType() const
{
    return program->nodes[static_cast<size_t> (program->root)].type;
}

bool SkinExpression::referencesContext (juce::StringRef path) const
{
    return std::any_of (program->nodes.begin(), program->nodes.end(), [&] (const auto& node)
    {
        return node.opcode == Opcode::context && node.path == path;
    });
}

std::optional<SkinExpressionValue> SkinExpression::evaluateWithBooleanContext (
    juce::StringRef pathRef, bool value) const
{
    const juce::String path (pathRef);
    SkinExpressionContext context;
    context.lookup = [programToUse = program, path, value] (std::string_view requested)
        -> std::optional<SkinExpressionValue>
    {
        const auto requestedPath = juce::String::fromUTF8 (
            requested.data(), static_cast<int> (requested.size()));
        if (requestedPath == path) return SkinExpressionValue (value);
        const auto found = std::find_if (
            programToUse->nodes.begin(), programToUse->nodes.end(), [&] (const auto& node)
            {
                return node.opcode == Opcode::context && node.path == requestedPath;
            });
        if (found == programToUse->nodes.end()) return std::nullopt;
        switch (found->type)
        {
            case SkinExpressionType::number: return SkinExpressionValue (1.0);
            case SkinExpressionType::boolean:
                return SkinExpressionValue (requestedPath == "state.enabled");
            case SkinExpressionType::string: return SkinExpressionValue (juce::String());
            case SkinExpressionType::colour:
                return SkinExpressionValue (juce::Colours::transparentBlack);
        }
        return std::nullopt;
    };
    return evaluate (context);
}

bool SkinExpression::changesWhenBooleanContextChanges (juce::StringRef path) const
{
    if (! referencesContext (path)) return false;
    const auto off = evaluateWithBooleanContext (path, false);
    const auto on = evaluateWithBooleanContext (path, true);
    return off && on && *off != *on;
}

bool SkinExpression::rebindTokens (const SkinTokenStore& tokens,
                                   SkinValidationReport& report)
{
    auto rebound = std::make_shared<SkinExpressionProgram> (*program);
    for (auto& node : rebound->nodes)
    {
        if (node.opcode != Opcode::constant || node.path.isEmpty()) continue;
        std::optional<SkinExpressionValue> value;
        if (node.tokenStringAsColour)
        {
            if (const auto* text = tokens.find<juce::String> (node.path))
                if (const auto colour = parseSkinCssColour (*text)) value = *colour;
        }
        else if (const auto* number = tokens.find<double> (node.path)) value = *number;
        else if (const auto* duration = tokens.find<int64_t> (node.path))
            value = static_cast<double> (*duration);
        else if (const auto* text = tokens.find<juce::String> (node.path)) value = *text;
        else if (const auto* colour = tokens.find<juce::Colour> (node.path)) value = *colour;
        if (! value)
        {
            report.addError (tokens.contains (node.path) ? "EXPR006" : "EXPR003",
                             rebound->file, rebound->pointer,
                             "Inherited expression token cannot be rebound: " + node.path + ".");
            return false;
        }
        if (typeOf (*value) != node.type)
        {
            report.addError ("EXPR006", rebound->file, rebound->pointer,
                             "Inherited expression token changed type: " + node.path + ".");
            return false;
        }
        node.constant = std::move (*value);
    }
    program = std::move (rebound);
    return true;
}

std::optional<SkinExpressionValue> SkinExpression::evaluate (
    const SkinExpressionContext& context, SkinValidationReport* runtimeWarnings) const
{
    return Evaluator (*program, context, runtimeWarnings).run();
}
