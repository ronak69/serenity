/*
 * Copyright (c) 2019-2020, Sergey Bugaev <bugaevc@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/EnumBits.h>
#include <AK/OwnPtr.h>
#include <AK/String.h>
#include <LibMarkdown/Block.h>
#include <LibMarkdown/ContainerBlock.h>

namespace Markdown {

enum class RenderExtension {
    DisableAll = 0,
    FragmentLinksInHeading = 0x1,
    PrependFileProtocolIfAbsolutePath = 0x2,
};

AK_ENUM_BITWISE_OPERATORS(RenderExtension);

class RenderExtensionConfig {
public:
    bool is_enabled(RenderExtension const& extensions) const
    {
        return has_flag(m_extensions, extensions);
    }

    void enable(RenderExtension const& extensions)
    {
        m_extensions |= extensions;
    }

    void disable(RenderExtension const& extensions)
    {
        m_extensions &= ~extensions;
    }

    void disable_all()
    {
        m_extensions = RenderExtension::DisableAll;
    }

private:
    RenderExtension m_extensions {
        // Render extensions that are enabled by default:
        RenderExtension::FragmentLinksInHeading
        | RenderExtension::PrependFileProtocolIfAbsolutePath
    };
};

class Document final {
public:
    Document(OwnPtr<ContainerBlock> container)
        : m_container(move(container))
    {
    }
    ByteString render_to_html(StringView extra_head_contents = ""sv, RenderExtensionConfig const& = {}) const;
    ByteString render_to_inline_html(RenderExtensionConfig const& = {}) const;
    ErrorOr<String> render_for_terminal(size_t view_width = 0) const;

    /*
     * Walk recursively through the document tree. Returning `RecursionDecision::Recurse` from
     * `Visitor::visit` proceeds with the next element of the pre-order walk, usually a child element.
     * Returning `RecursionDecision::Continue` skips the subtree, and usually proceeds with the next
     * sibling. Returning `RecursionDecision::Break` breaks the recursion, with no further calls to
     * any of the `Visitor::visit` methods.
     *
     * Note that `walk()` will only return `RecursionDecision::Continue` or `RecursionDecision::Break`.
     */
    RecursionDecision walk(Visitor&) const;

    static OwnPtr<Document> parse(StringView);

private:
    OwnPtr<ContainerBlock> m_container;
};

}
