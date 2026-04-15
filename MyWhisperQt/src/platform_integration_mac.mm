#include "platform_integration.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>
#import <dispatch/dispatch.h>

#include <unistd.h>

namespace {
NSString *toNSString(const QString &text) {
    return [NSString stringWithUTF8String:text.toUtf8().constData()];
}

void playNamedSound(NSString *name) {
    NSSound *sound = [NSSound soundNamed:name];
    [sound play];
}
}

namespace PlatformIntegration {
void initializeSounds() {
}

bool ensureAccessibilityPermissionPrompted() {
    NSDictionary *options = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt: @YES};
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}

void copyToClipboard(const QString &text) {
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    [pasteboard setString:toNSString(text) forType:NSPasteboardTypeString];
}

void pasteText(const QString &text) {
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    NSArray<NSPasteboardItem *> *previousItems = [[pasteboard pasteboardItems] copy];

    [pasteboard clearContents];
    [pasteboard setString:toNSString(text) forType:NSPasteboardTypeString];
    usleep(50000);

    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    CGEventRef keyDown = CGEventCreateKeyboardEvent(source, static_cast<CGKeyCode>(0x09), true);
    CGEventRef keyUp = CGEventCreateKeyboardEvent(source, static_cast<CGKeyCode>(0x09), false);
    CGEventSetFlags(keyDown, kCGEventFlagMaskCommand);
    CGEventSetFlags(keyUp, kCGEventFlagMaskCommand);
    CGEventPost(kCGHIDEventTap, keyDown);
    CGEventPost(kCGHIDEventTap, keyUp);

    if (keyDown) {
        CFRelease(keyDown);
    }
    if (keyUp) {
        CFRelease(keyUp);
    }
    if (source) {
        CFRelease(source);
    }

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(0.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [pasteboard clearContents];
        for (NSPasteboardItem *item in previousItems) {
            [pasteboard writeObjects:@[item]];
        }
    });
}

void playStartSound() {
    playNamedSound(@"Pop");
}

void playStopSound() {
    playNamedSound(@"Tink");
}

void playSuccessSound() {
    playNamedSound(@"Glass");
}

void playErrorSound() {
    playNamedSound(@"Basso");
}
}
