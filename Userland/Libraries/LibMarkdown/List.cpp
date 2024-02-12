/*
 * Copyright (c) 2019-2020, Sergey Bugaev <bugaevc@serenityos.org>
 * Copyright (c) 2021, Peter Elliott <pelliott@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Forward.h>
#include <AK/StringBuilder.h>
#include <LibMarkdown/List.h>
#include <LibMarkdown/Paragraph.h>
#include <LibMarkdown/Visitor.h>

namespace Markdown {

ByteString List::render_to_html(bool) const
{
    StringBuilder builder;

    char const* tag = m_is_ordered ? "ol" : "ul";
    builder.appendff("<{}", tag);

    if (m_start_number != 1)
        builder.appendff(" start=\"{}\"", m_start_number);

    builder.append(">\n"sv);

    for (auto& item : m_items) {
        builder.append("<li>"sv);
        if (!m_is_tight || (item->blocks().size() != 0 && !dynamic_cast<Paragraph const*>(item->blocks()[0].ptr())))
            builder.append('\n');
        builder.append(item->render_to_html(m_is_tight));
        builder.append("</li>\n"sv);
    }

    builder.appendff("</{}>\n", tag);

    return builder.to_byte_string();
}

Vector<ByteString> List::render_lines_for_terminal(size_t view_width) const
{
    Vector<ByteString> lines;

    int i = 0;
    for (auto& item : m_items) {
        auto item_lines = item->render_lines_for_terminal(view_width);
        auto first_line = item_lines.take_first();

        StringBuilder builder;
        builder.append("  "sv);
        if (m_is_ordered)
            builder.appendff("{}.", ++i);
        else
            builder.append('*');
        auto item_indentation = builder.length();

        builder.append(first_line);

        lines.append(builder.to_byte_string());

        for (auto& line : item_lines) {
            builder.clear();
            builder.append(ByteString::repeated(' ', item_indentation));
            builder.append(line);
            lines.append(builder.to_byte_string());
        }
    }

    return lines;
}

RecursionDecision List::walk(Visitor& visitor) const
{
    RecursionDecision rd = visitor.visit(*this);
    if (rd != RecursionDecision::Recurse)
        return rd;

    for (auto const& block : m_items) {
        rd = block->walk(visitor);
        if (rd == RecursionDecision::Break)
            return rd;
    }

    return RecursionDecision::Continue;
}

OwnPtr<List> List::parse(LineIterator& lines, bool is_interrupting_paragraph)
{
    Vector<OwnPtr<ContainerBlock>> items;

    bool first = true;
    bool is_ordered = false;

    bool is_tight = true;
    bool has_trailing_blank_lines = false;
    size_t start_number = 1;

    while (!lines.is_end()) {

        size_t offset = 0;

        StringView line = *lines;

        bool appears_unordered = false;

        while (offset < line.length() && line[offset] == ' ')
            ++offset;

        // Optional initial indentation of maximum three spaces (example 289)
        if (offset > 3) {
            if (first)
                return {};

            break;
        }

        if (offset + 2 <= line.length()) {
            if (line[offset + 1] == ' ' && (line[offset] == '*' || line[offset] == '-' || line[offset] == '+')) {
                appears_unordered = true;
                offset++;
            }
        }

        bool appears_ordered = false;
        for (size_t i = offset; i < 10 && i < line.length(); i++) {
            char ch = line[i];
            if ('0' <= ch && ch <= '9')
                continue;
            if (ch == '.' || ch == ')')
                if (i + 1 < line.length() && line[i + 1] == ' ') {
                    auto maybe_start_number = line.substring_view(offset, i - offset).to_number<size_t>();
                    if (!maybe_start_number.has_value())
                        break;
                    if (first) {
                        start_number = maybe_start_number.value();
                        // Only ordered lists starting with 1 can iterrupt a paragraph (example 304)
                        if (is_interrupting_paragraph && start_number != 1)
                            return {};
                    }
                    appears_ordered = true;
                    offset = i + 1;
                }
            break;
        }

        VERIFY(!(appears_unordered && appears_ordered));
        if (!appears_unordered && !appears_ordered) {
            if (first)
                return {};

            break;
        }

        VERIFY(line[offset] == ' ');

        size_t fallback_context_indent = offset + 1;

        while (offset < line.length() && line[offset] == ' ')
            offset++;

        size_t context_indent = offset;

        // Item starting with indented code (example 273)
        if (1 + offset - fallback_context_indent > 4)
            context_indent = fallback_context_indent;

        if (first) {
            is_ordered = appears_ordered;
        } else if (appears_ordered != is_ordered) {
            break;
        }

        is_tight = is_tight && !has_trailing_blank_lines;

        lines.push_context(LineIterator::Context::list_item(context_indent));

        auto list_item = ContainerBlock::parse(lines);
        is_tight = is_tight && !list_item->has_blank_lines();
        has_trailing_blank_lines = has_trailing_blank_lines || list_item->has_trailing_blank_lines();
        items.append(move(list_item));

        lines.pop_context();

        first = false;
    }

    return make<List>(move(items), is_ordered, is_tight, start_number);
}

}
