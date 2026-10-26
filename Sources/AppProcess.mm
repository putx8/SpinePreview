#import <AppKit/AppKit.h>
#include "AppProcess.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <mach-o/dyld.h>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <mutex>
#include <deque>

namespace {
std::mutex launchMutex;
std::deque<std::string> launchErrors;
}

@interface SpineApplicationMenuActions : NSObject
@property(nonatomic) uint32_t newInstanceEvent;
- (void)newInstance:(id)sender;
@end
@implementation SpineApplicationMenuActions
- (void)newInstance:(id)sender {
    (void)sender;
    NSEvent *event = NSApp.currentEvent;
    if (event.type == NSEventTypeKeyDown && event.isARepeat) return;
    SDL_Event request{};
    request.type = self.newInstanceEvent;
    if (!SDL_PushEvent(&request)) {
        std::lock_guard<std::mutex> lock(launchMutex);
        launchErrors.emplace_back(SDL_GetError());
    }
}
@end

void installApplicationMenu(uint32_t newInstanceEvent) {
    static SpineApplicationMenuActions *actions;
    actions = [SpineApplicationMenuActions new];
    actions.newInstanceEvent = newInstanceEvent;
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem *newItem = [[NSMenuItem alloc] initWithTitle:@"New Instance" action:@selector(newInstance:) keyEquivalent:@"n"];
    newItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    newItem.target = actions;
    [fileMenu addItem:newItem];
    NSMenuItem *fileItem = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
    fileItem.submenu = fileMenu;
    [NSApp.mainMenu insertItem:fileItem atIndex:MIN(1, NSApp.mainMenu.numberOfItems)];
}

std::string takeApplicationLaunchError() {
    std::lock_guard<std::mutex> lock(launchMutex);
    if (launchErrors.empty()) return {};
    auto error = std::move(launchErrors.front()); launchErrors.pop_front();
    return error;
}

std::string applicationExecutablePath() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        throw std::runtime_error("Cannot locate the application executable");
    return std::filesystem::weakly_canonical(buffer.data()).string();
}

bool launchNewApplication(SDL_Window *sourceWindow, std::string &error) {
    try {
        int x = 0, y = 0, width = 0, height = 0;
        if (!SDL_GetWindowPosition(sourceWindow, &x, &y)) { error = SDL_GetError(); return false; }
        if (!SDL_GetWindowSize(sourceWindow, &width, &height)) { error = SDL_GetError(); return false; }
        SDL_Rect usable{};
        int nextWindowX = x+50, nextWindowY = y+50;
        if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(sourceWindow), &usable)) {
            nextWindowX = std::clamp(nextWindowX, usable.x, usable.x+std::max(0, usable.w-width));
            nextWindowY = std::clamp(nextWindowY, usable.y, usable.y+std::max(0, usable.h-height));
            // At the bottom-right edge, cascade back instead of covering the source.
            if (nextWindowX == x && nextWindowY == y) {
                nextWindowX = std::max(usable.x, x-50);
                nextWindowY = std::max(usable.y, y-50);
            }
        }
        const auto bundlePath = std::filesystem::path(applicationExecutablePath()).parent_path().parent_path().parent_path();
        NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:bundlePath.c_str()] isDirectory:YES];
        NSWorkspaceOpenConfiguration *configuration = [NSWorkspaceOpenConfiguration configuration];
        configuration.createsNewApplicationInstance = YES;
        configuration.activates = YES;
        configuration.arguments = @[@"--window-position", [NSString stringWithFormat:@"%d", nextWindowX],
                                     [NSString stringWithFormat:@"%d", nextWindowY], @"--window-size",
                                     [NSString stringWithFormat:@"%d", width], [NSString stringWithFormat:@"%d", height]];
        [[NSWorkspace sharedWorkspace] openApplicationAtURL:url configuration:configuration
            completionHandler:^(NSRunningApplication *, NSError *launchError) {
                if (launchError) {
                    std::lock_guard<std::mutex> lock(launchMutex);
                    launchErrors.emplace_back(launchError.localizedDescription.UTF8String);
                }
            }];
        return true;
    } catch (const std::exception &e) { error = e.what(); return false; }
}
