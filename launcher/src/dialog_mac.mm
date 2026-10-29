#include "dialog.h"

#import <Cocoa/Cocoa.h>

namespace nla {
namespace {

NSString* to_ns(const std::wstring& text) {
    auto utf8 = narrow(text);
    return [NSString stringWithUTF8String:utf8.c_str()];
}

}  // namespace

DialogAnswer show_native_dialog(DialogKind kind, SDL_Window*, const std::wstring& title,
                                const fs::path& start) {
    DialogAnswer answer;
    answer.shown = true;
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        if (!title.empty()) [panel setTitle:to_ns(title)];
        panel.canChooseFiles = kind == DialogKind::Image;
        panel.canChooseDirectories = kind == DialogKind::Folder;
        panel.allowsMultipleSelection = NO;
        panel.resolvesAliases = YES;
        if (kind == DialogKind::Image) {
            // Deprecated in macOS 12 in favour of allowedContentTypes, but still honoured
            // and it needs no UniformTypeIdentifiers deployment bump.
            panel.allowedFileTypes = @[
                @"iso", @"ciso", @"rvz", @"wia", @"gcz", @"wbfs", @"tgc", @"nfs"
            ];
        }

        if (!start.empty()) {
            std::error_code ec;
            bool is_folder = fs::is_directory(start, ec);
            auto folder = is_folder ? start : start.parent_path();
            if (!folder.empty())
                [panel setDirectoryURL:[NSURL fileURLWithPath:to_ns(folder.wstring())
                                                    isDirectory:YES]];
            if (!is_folder)
                [panel setNameFieldStringValue:to_ns(start.filename().wstring())];
        }

        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = [panel URL];
            if (url) answer.path = fs::path([[url path] UTF8String]);
        }
    }
    return answer;
}

}  // namespace nla
