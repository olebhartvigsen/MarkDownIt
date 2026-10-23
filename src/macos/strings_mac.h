// One place per user-visible string the mac app shows (find spec: all
// user-visible strings are localizable). The find bar's strings live
// here, so a translation can replace one header without touching the
// behaviour.
#pragma once

#import <Foundation/Foundation.h>

namespace macstrings {

static NSString* const kFindPlaceholder = @"Find";
static NSString* const kReplacePlaceholder = @"Replace";
static NSString* const kPreviousMatch = @"Previous match";
static NSString* const kNextMatch = @"Next match";
static NSString* const kReplaceTheMatch = @"Replace the match";
static NSString* const kReplaceEveryMatch = @"Replace every match";
static NSString* const kCloseFindBar = @"Close the find bar";
static NSString* const kMatchCase = @"Match case";
static NSString* const kWholeWord = @"Whole word";
static NSString* const kCloseFindBarLabel = @"Close find bar";
static NSString* const kSearchText = @"Search text";
static NSString* const kReplaceText = @"Replace text";
static NSString* const kRecentSearches = @"Recent searches";
static NSString* const kNoRecentSearches = @"No recent searches";

}  // namespace macstrings
