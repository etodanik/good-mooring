/*
 * Copyright (c) 2017-2025 The Forge Interactive Inc.
 *
 * This file is part of The-Forge
 * (see https://github.com/ConfettiFX/The-Forge).
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#import "macOSAppDelegate.h"

#import <IOKit/pwr_mgt/IOPMLib.h>
#define DISPATCH_APPROACH
// #define LOOP_IN_FINISH_LAUNCHING

static IOPMAssertionID systemSleepAssertionID, displaySleepAssertionID;
@protocol RenderDestinationProvider
- (void)draw;
- (void)didResize:(CGSize)size;
- (void)didMiniaturize;
- (void)didDeminiaturize;
- (void)didFocusChange:(bool)active;
@end

@interface GameController: NSObject <RenderDestinationProvider>
@end

@interface AppDelegate ()

@end

@implementation AppDelegate
{
    GameController*   myController;
    dispatch_source_t renderTimer;
}

- (void)drawFunc
{
    @autoreleasepool
    {
        [myController draw];
    }
}

- (void)applicationDidFinishLaunching:(NSNotification*)aNotification
{
    // prevent Energy-Saver to turn off the display
    IOPMAssertionCreateWithName(kIOPMAssertPreventUserIdleSystemSleep, kIOPMAssertionLevelOn, CFSTR("Running The-Forge"),
                                &systemSleepAssertionID);
    IOPMAssertionCreateWithName(kIOPMAssertPreventUserIdleDisplaySleep, kIOPMAssertionLevelOn, CFSTR("Running The-Forge"),
                                &displaySleepAssertionID);

    myController = [GameController new];

#ifdef DISPATCH_APPROACH
    __weak AppDelegate* weakSelf = self;
    renderTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    // Benchmarks remove the playback cap; the main queue still services UI events.
    BOOL benchmark = [[[NSProcessInfo processInfo] arguments] containsObject:@"--benchmark-unthrottled"];
    dispatch_source_set_timer(renderTimer, dispatch_time(DISPATCH_TIME_NOW, 0), benchmark ? NSEC_PER_MSEC : NSEC_PER_SEC / 30,
                              benchmark ? 0 : NSEC_PER_MSEC);
    dispatch_source_set_event_handler(renderTimer, ^{
        [weakSelf drawFunc];
    });
    dispatch_resume(renderTimer);
#endif

#ifdef LOOP_IN_FINISH_LAUNCHING
    for (;;)
    {
        NSEvent* Event;
        @autoreleasepool
        {
            while ((Event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:nil inMode:NSDefaultRunLoopMode dequeue:TRUE]))
                [NSApp sendEvent:Event];

            [myController draw];
        }
    }
    myController = nil;
#endif
}

- (void)applicationWillTerminate:(NSNotification*)aNotification
{
    if (renderTimer)
        dispatch_source_cancel(renderTimer);
    if (systemSleepAssertionID > 0)
    {
        IOPMAssertionRelease(systemSleepAssertionID);
    }
    if (displaySleepAssertionID > 0)
    {
        IOPMAssertionRelease(displaySleepAssertionID);
    }
    // Insert code here to tear down your application
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
    return TRUE;
}

- (void)applicationDidEnterBackground:(NSNotification*)aNotification
{
    [myController didFocusChange:FALSE];
}

- (void)applicationWillEnterForeground:(NSNotification*)aNotification
{
    [myController didFocusChange:TRUE];
}

@end
