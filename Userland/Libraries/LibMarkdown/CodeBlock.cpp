/*
 * Copyright (c) 2019-2020, Sergey Bugaev <bugaevc@serenityos.org>
 * Copyright (c) 2022, Peter Elliott <pelliott@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Forward.h>
#include <AK/StringBuilder.h>
#include <LibJS/MarkupGenerator.h>
#include <LibMarkdown/CodeBlock.h>
#include <LibMarkdown/Visitor.h>
#include <LibRegex/Regex.h>

namespace Markdown {

ByteString CodeBlock::render_to_html(RenderExtensionConfig const&, bool) const
{
    StringBuilder builder;

    builder.append("<pre>"sv);

    if (m_style.length() >= 2)
        builder.append("<strong>"sv);
    else if (m_style.length() >= 2)
        builder.append("<em>"sv);

    if (m_language.is_empty())
        builder.append("<code>"sv);
    else
        builder.appendff("<code class=\"language-{}\">", escape_html_entities(m_language));

    if (m_language == "js") {
        auto html_or_error = JS::MarkupGenerator::html_from_source(m_code);
        if (html_or_error.is_error()) {
            warnln("Could not render js code to html: {}", html_or_error.error());
            builder.append(escape_html_entities(m_code));
        } else {
            builder.append(html_or_error.release_value());
        }
    } else {
        builder.append(escape_html_entities(m_code));
    }

    builder.append("</code>"sv);

    if (m_style.length() >= 2)
        builder.append("</strong>"sv);
    else if (m_style.length() >= 2)
        builder.append("</em>"sv);

    builder.append("</pre>\n"sv);

    return builder.to_byte_string();
}

Vector<ByteString> CodeBlock::render_lines_for_terminal(size_t) const
{
    Vector<ByteString> lines;

    // Do not indent too much if we are in the synopsis
    auto indentation = "    "sv;
    if (m_current_section != nullptr) {
        auto current_section_name = m_current_section->render_lines_for_terminal()[0];
        if (current_section_name.contains("SYNOPSIS"sv))
            indentation = "  "sv;
    }

    for (auto const& line : m_code.split('\n', SplitBehavior::KeepEmpty))
        lines.append(ByteString::formatted("{}{}", indentation, line));

    return lines;
}

RecursionDecision CodeBlock::walk(Visitor& visitor) const
{
    RecursionDecision rd = visitor.visit(*this);
    if (rd != RecursionDecision::Recurse)
        return rd;

    rd = visitor.visit(m_code);
    if (rd != RecursionDecision::Recurse)
        return rd;

    // Don't recurse on m_language and m_style.

    // Normalize return value.
    return RecursionDecision::Continue;
}

// Separate regexes are used here because,
// - Info strings for backtick code blocks cannot contain backticks (example 145)
// - Info strings for tilde code blocks can contain backticks and tildes (example 146)
static Regex<ECMA262> backtick_open_fence_re(R"#(^ {0,3}(\`{3,})\s*([\*_]*)\s*([^\*_\s\`]*)[^\`]*$)#");
static Regex<ECMA262> tilde_open_fence_re(R"#(^ {0,3}(\~{3,})\s*([\*_]*)\s*([^\*_\s]*).*$)#");
static Regex<ECMA262> close_fence_re(R"#(^ {0,3}(([\`\~])\2{2,})\s*$)#");

static Optional<size_t> line_block_prefix(StringView const& line)
{
    size_t characters = 0;
    size_t indents = 0;

    for (char ch : line) {
        if (indents == 4)
            break;

        if (ch == ' ') {
            ++characters;
            ++indents;
        } else if (ch == '\t') {
            ++characters;
            indents = 4;
        } else {
            break;
        }
    }

    if (indents == 4)
        return characters;

    return {};
}

OwnPtr<CodeBlock> CodeBlock::parse(LineIterator& lines, Heading* current_section, bool is_interrupting_paragraph)
{
    if (lines.is_end())
        return {};

    StringView line = *lines;

    if (auto backtick_match_result = backtick_open_fence_re.match(line); backtick_match_result.success)
        return parse_backticks(lines, current_section, backtick_match_result);
    else if (auto tilde_match_result = tilde_open_fence_re.match(line); tilde_match_result.success)
        return parse_backticks(lines, current_section, tilde_match_result);

    // An indented code block cannot interrupt a paragraph (example 113)
    if (is_interrupting_paragraph)
        return {};

    if (line_block_prefix(line).has_value())
        return parse_indent(lines);

    return {};
}

OwnPtr<CodeBlock> CodeBlock::parse_backticks(LineIterator& lines, Heading* current_section, RegexResult match_result)
{
    StringView line = *lines;

    // Our Markdown extension: we allow
    // specifying a style and a language
    // for a code block, like so:
    //
    // ```**sh**
    // $ echo hello friends!
    // ````
    //
    // The code block will be made bold,
    // and if possible syntax-highlighted
    // as appropriate for a shell script.

    auto matches = match_result.capture_group_matches[0];
    auto fence = matches[0].view.string_view();
    auto style = matches[1].view.string_view();
    auto language = matches[2].view.string_view();

    size_t fence_indent = 0;
    while (fence_indent < line.length() && line[fence_indent] == ' ')
        ++fence_indent;

    ++lines;

    StringBuilder builder;

    while (true) {
        if (lines.is_end())
            break;
        line = *lines;
        ++lines;

        auto close_match = close_fence_re.match(line);
        if (close_match.success) {
            auto close_fence = close_match.capture_group_matches[0][0].view.string_view();
            if (close_fence[0] == fence[0] && close_fence.length() >= fence.length())
                break;
        }

        // If the opening fence is indented, content lines will have equivalent
        // opening indentation removed, if present. (example 131, 132 and 133)
        size_t offset = 0;
        while (offset < line.length() && offset < fence_indent && line[offset] == ' ')
            ++offset;

        builder.append(line.substring_view(offset));
        builder.append('\n');
    }

    return make<CodeBlock>(language, style, builder.to_byte_string(), current_section);
}

OwnPtr<CodeBlock> CodeBlock::parse_indent(LineIterator& lines)
{
    StringBuilder builder;
    u32 blank_lines_after_last_chunk = 0;

    while (true) {
        if (lines.is_end())
            break;
        StringView line = *lines;

        auto prefix_length = line_block_prefix(line);

        if (!prefix_length.has_value() || prefix_length.value() == line.length()) {
            // An indented code block is composed of one or more indented chunks
            // separated by blank lines. (example 111 and 117)
            if (line == ""sv || line.is_whitespace()) {
                ++lines;
                ++blank_lines_after_last_chunk;
                continue;
            }

            break;
        }

        line = line.substring_view(prefix_length.value());
        ++lines;

        if (blank_lines_after_last_chunk) {
            builder.append_repeated('\n', blank_lines_after_last_chunk);
            blank_lines_after_last_chunk = 0;
        }

        builder.append(line);
        builder.append('\n');
    }

    return make<CodeBlock>("", "", builder.to_byte_string(), nullptr);
}
}
