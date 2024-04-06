/*
 * Copyright (c) 2021, Peter Elliott <pelliott@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Forward.h>
#include <LibMarkdown/BlockQuote.h>
#include <LibMarkdown/CodeBlock.h>
#include <LibMarkdown/ContainerBlock.h>
#include <LibMarkdown/Heading.h>
#include <LibMarkdown/HorizontalRule.h>
#include <LibMarkdown/List.h>
#include <LibMarkdown/Paragraph.h>
#include <LibMarkdown/Table.h>
#include <LibMarkdown/Visitor.h>
#include <LibRegex/Regex.h>

namespace Markdown {

ByteString ContainerBlock::render_to_html(RenderExtensionConfig const& render_extension_config, bool tight) const
{
    StringBuilder builder;

    for (size_t i = 0; i + 1 < m_blocks.size(); ++i) {
        auto s = m_blocks[i]->render_to_html(render_extension_config, tight);
        builder.append(s);
    }

    // I don't like this edge case.
    if (m_blocks.size() != 0) {
        auto& block = m_blocks[m_blocks.size() - 1];
        auto s = block->render_to_html(render_extension_config, tight);
        if (tight && dynamic_cast<Paragraph const*>(block.ptr())) {
            builder.append(s.substring_view(0, s.length() - 1));
        } else {
            builder.append(s);
        }
    }

    return builder.to_byte_string();
}

Vector<ByteString> ContainerBlock::render_lines_for_terminal(size_t view_width) const
{
    Vector<ByteString> lines;

    for (auto& block : m_blocks) {
        for (auto& line : block->render_lines_for_terminal(view_width))
            lines.append(move(line));
    }

    return lines;
}

RecursionDecision ContainerBlock::walk(Visitor& visitor) const
{
    RecursionDecision rd = visitor.visit(*this);
    if (rd != RecursionDecision::Recurse)
        return rd;

    for (auto const& block : m_blocks) {
        rd = block->walk(visitor);
        if (rd == RecursionDecision::Break)
            return rd;
    }

    return RecursionDecision::Continue;
}

template<class CodeBlock>
static bool try_parse_block(LineIterator& lines, Vector<NonnullOwnPtr<Block>>& blocks, Heading* current_section, bool is_interrupting_paragraph)
{
    OwnPtr<CodeBlock> block = CodeBlock::parse(lines, current_section, is_interrupting_paragraph);
    if (!block)
        return false;
    blocks.append(block.release_nonnull());
    return true;
}

template<class BlockType>
static bool try_parse_block(LineIterator& lines, Vector<NonnullOwnPtr<Block>>& blocks, bool is_interrupting_paragraph)
{
    OwnPtr<BlockType> block = BlockType::parse(lines, is_interrupting_paragraph);
    if (!block)
        return false;
    blocks.append(block.release_nonnull());
    return true;
}

template<typename BlockType>
static bool try_parse_block(LineIterator& lines, Vector<NonnullOwnPtr<Block>>& blocks)
{
    OwnPtr<BlockType> block = BlockType::parse(lines);
    if (!block)
        return false;
    blocks.append(block.release_nonnull());
    return true;
}

namespace {
Optional<size_t> try_parse_setext_heading_underline(StringView maybe_underline)
{
    static Regex<ECMA262> regex_setext_heading_underline_h1(R"~(^ {0,3}=+\s*$)~");
    static Regex<ECMA262> regex_setext_heading_underline_h2(R"~(^ {0,3}-+\s*$)~");

    if (regex_setext_heading_underline_h1.match(maybe_underline).success)
        return 1;

    if (regex_setext_heading_underline_h2.match(maybe_underline).success)
        return 2;

    return {};
}
}

OwnPtr<ContainerBlock> ContainerBlock::parse(LineIterator& lines)
{
    Vector<NonnullOwnPtr<Block>> blocks;

    StringBuilder paragraph_text;
    Heading* current_section = nullptr;

    auto flush_paragraph = [&] {
        if (paragraph_text.is_empty())
            return;
        auto paragraph = make<Paragraph>(Text::parse(paragraph_text.to_byte_string()));
        blocks.append(move(paragraph));
        paragraph_text.clear();
    };

    bool has_blank_lines = false;
    bool has_trailing_blank_lines = false;

    while (true) {
        if (lines.is_end())
            break;

        if ((*lines).is_whitespace()) {
            has_trailing_blank_lines = true;
            ++lines;

            flush_paragraph();
            continue;
        } else {
            has_blank_lines = has_blank_lines || has_trailing_blank_lines;
        }

        bool is_interrupting_paragraph = not paragraph_text.is_empty();

        Optional<size_t> setext_heading_level;
        if (is_interrupting_paragraph)
            setext_heading_level = try_parse_setext_heading_underline(*lines);

        bool heading = false;
        if ((heading = try_parse_block<Heading>(lines, blocks)))
            current_section = dynamic_cast<Heading*>(blocks.last().ptr());

        bool any = heading
            || try_parse_block<Table>(lines, blocks)
            || (not setext_heading_level.has_value() && try_parse_block<HorizontalRule>(lines, blocks))
            // CodeBlock needs to know the current section's name for proper indentation
            || try_parse_block<CodeBlock>(lines, blocks, current_section, is_interrupting_paragraph)
            || try_parse_block<List>(lines, blocks, is_interrupting_paragraph)
            || try_parse_block<CommentBlock>(lines, blocks)
            || try_parse_block<BlockQuote>(lines, blocks);

        if (any) {
            if (!paragraph_text.is_empty()) {
                auto last_block = blocks.take_last();
                flush_paragraph();
                blocks.append(move(last_block));
            }
            continue;
        } else if (setext_heading_level.has_value()) {
            OwnPtr<Heading> setext_heading = make<Heading>(Text::parse(paragraph_text.string_view()), setext_heading_level.value());
            current_section = setext_heading.ptr();
            blocks.append(setext_heading.release_nonnull());

            paragraph_text.clear();
            ++lines;
            continue;
        }

        if (!paragraph_text.is_empty())
            paragraph_text.append('\n');
        paragraph_text.append(*lines++);
    }

    flush_paragraph();

    return make<ContainerBlock>(move(blocks), has_blank_lines, has_trailing_blank_lines);
}

}
