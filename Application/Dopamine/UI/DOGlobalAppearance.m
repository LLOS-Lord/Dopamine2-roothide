//
//  GlobalAppearance.m
//  Dopamine
//
//  Created by Lars Fröder on 10.10.23.
//

#import "DOGlobalAppearance.h"
#import <CoreGraphics/CoreGraphics.h>
#import "DOThemeManager.h"

static UIColor *DOUTerminalBackgroundColor(void)
{
    return [UIColor colorWithRed:0.985 green:0.982 blue:0.975 alpha:1.0];
}

static UIColor *DOUTerminalGreenColor(void)
{
    return [UIColor colorWithRed:0.25 green:0.88 blue:0.34 alpha:1.0];
}

static UIColor *DOUTerminalTextColor(void)
{
    return [UIColor colorWithRed:0.08 green:0.08 blue:0.08 alpha:1.0];
}

static UIColor *DOUTerminalMutedColor(void)
{
    return [UIColor colorWithRed:0.38 green:0.38 blue:0.38 alpha:1.0];
}

static UIColor *DOUTerminalRuleColor(void)
{
    return [UIColor colorWithRed:0.18 green:0.18 blue:0.18 alpha:0.38];
}

static NSString *DOUTerminalTitleCaseWord(NSString *word)
{
    if (word.length == 0) return word;
    NSMutableString *result = [word mutableCopy];
    BOOL capitalizeNext = YES;
    for (NSUInteger index = 0; index < result.length; index++) {
        unichar character = [result characterAtIndex:index];
        if (character == '-' || character == '_' || character == '/' || [[NSCharacterSet whitespaceCharacterSet] characterIsMember:character]) {
            capitalizeNext = YES;
            continue;
        }
        if (capitalizeNext && [[NSCharacterSet letterCharacterSet] characterIsMember:character]) {
            [result replaceCharactersInRange:NSMakeRange(index, 1) withString:[[NSString stringWithCharacters:&character length:1] uppercaseString]];
            capitalizeNext = NO;
        } else if ([[NSCharacterSet letterCharacterSet] characterIsMember:character]) {
            capitalizeNext = NO;
        }
    }
    return result;
}

static NSString *DOUTerminalDisplayCommand(NSString *command)
{
    if (command.length == 0) return command;
    // `--` belongs to the internal command syntax, not the visual terminal UI.
    NSString *visualCommand = [[command stringByReplacingOccurrencesOfString:@"--" withString:@""] stringByReplacingOccurrencesOfString:@"-" withString:@" "];
    return DOUTerminalTitleCaseWord(visualCommand.lowercaseString);
}

static NSString *DOUTerminalDisplayKey(NSString *key)
{
    return DOUTerminalTitleCaseWord(key.lowercaseString);
}

@interface DOUTerminalCommandView : UIControl
@property (nonatomic, copy) void (^handler)(void);
@property (nonatomic, strong) UILabel *markerLabel;
@property (nonatomic, strong) UILabel *commandLabel;
@property (nonatomic, strong) UILabel *valueLabel;
@property (nonatomic, assign) BOOL commandEnabled;
@property (nonatomic, assign) BOOL primaryCommand;
- (instancetype)initWithCommand:(NSString *)command detail:(NSString *)detail enabled:(BOOL)enabled handler:(void (^)(void))handler;
- (instancetype)initWithCommand:(NSString *)command detail:(NSString *)detail enabled:(BOOL)enabled compact:(BOOL)compact handler:(void (^)(void))handler;
@end

@implementation DOUTerminalCommandView

- (instancetype)initWithCommand:(NSString *)command detail:(NSString *)detail enabled:(BOOL)enabled handler:(void (^)(void))handler
{
    return [self initWithCommand:command detail:detail enabled:enabled compact:NO handler:handler];
}

- (instancetype)initWithCommand:(NSString *)command detail:(NSString *)detail enabled:(BOOL)enabled compact:(BOOL)compact handler:(void (^)(void))handler
{
    self = [super initWithFrame:CGRectZero];
    if (!self) return nil;
    self.translatesAutoresizingMaskIntoConstraints = NO;
    self.handler = handler;
    self.enabled = enabled;
    self.accessibilityTraits = enabled ? UIAccessibilityTraitButton : UIAccessibilityTraitNotEnabled;

    self.commandEnabled = enabled;

    UILabel *marker = [[UILabel alloc] init];
    marker.translatesAutoresizingMaskIntoConstraints = NO;
    marker.text = @"";
    marker.textColor = UIColor.clearColor;
    marker.alpha = 0.0;
    marker.font = [UIFont monospacedSystemFontOfSize:14 weight:UIFontWeightBold];
    self.markerLabel = marker;
    [self addSubview:marker];

    NSRange separator = [command rangeOfString:@"  "];
    NSString *rawCommandText = separator.location == NSNotFound ? command : [command substringToIndex:separator.location];
    NSString *rawValueText = separator.location == NSNotFound ? @"" : [[command substringFromIndex:separator.location + 2] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    NSString *commandText = DOUTerminalDisplayCommand(rawCommandText);
    NSString *valueText = DOUTerminalDisplayCommand(rawValueText);
    BOOL hasValue = valueText.length > 0;

    UILabel *commandLabel = [[UILabel alloc] init];
    commandLabel.translatesAutoresizingMaskIntoConstraints = NO;
    commandLabel.text = commandText;
    self.primaryCommand = enabled && [commandText.lowercaseString isEqualToString:@"jailbreak"];
    commandLabel.textColor = self.primaryCommand ? DOUTerminalGreenColor() : (enabled ? DOUTerminalTextColor() : [DOUTerminalTextColor() colorWithAlphaComponent:0.42]);
    commandLabel.font = [UIFont monospacedSystemFontOfSize:15 weight:UIFontWeightMedium];
    commandLabel.adjustsFontForContentSizeCategory = YES;
    commandLabel.numberOfLines = 1;
    commandLabel.lineBreakMode = NSLineBreakByClipping;
    self.commandLabel = commandLabel;
    [self addSubview:commandLabel];

    UILabel *valueLabel = [[UILabel alloc] init];
    valueLabel.translatesAutoresizingMaskIntoConstraints = NO;
    valueLabel.text = valueText;
    valueLabel.textColor = enabled ? DOUTerminalTextColor() : [DOUTerminalTextColor() colorWithAlphaComponent:0.42];
    valueLabel.font = [UIFont monospacedSystemFontOfSize:15 weight:UIFontWeightMedium];
    valueLabel.adjustsFontForContentSizeCategory = YES;
    valueLabel.numberOfLines = 1;
    valueLabel.lineBreakMode = NSLineBreakByClipping;
    valueLabel.hidden = !hasValue;
    self.valueLabel = valueLabel;
    [self addSubview:valueLabel];

    UILabel *detailLabel = nil;
    if (!compact) {
        detailLabel = [[UILabel alloc] init];
        detailLabel.translatesAutoresizingMaskIntoConstraints = NO;
        detailLabel.text = detail;
        detailLabel.textColor = enabled ? DOUTerminalMutedColor() : [DOUTerminalMutedColor() colorWithAlphaComponent:0.55];
        detailLabel.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightRegular];
        detailLabel.adjustsFontForContentSizeCategory = YES;
        detailLabel.numberOfLines = 0;
        [self addSubview:detailLabel];
    }

    if (compact) {
        NSMutableArray<NSLayoutConstraint *> *constraints = [NSMutableArray array];
        [constraints addObjectsFromArray:@[
            [self.heightAnchor constraintGreaterThanOrEqualToConstant:34.0],
            [marker.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
            [marker.topAnchor constraintEqualToAnchor:self.topAnchor constant:5.0],
            [marker.widthAnchor constraintEqualToConstant:18.0],
            [commandLabel.leadingAnchor constraintEqualToAnchor:marker.trailingAnchor constant:7.0],
            [commandLabel.topAnchor constraintEqualToAnchor:self.topAnchor constant:4.0],
            [commandLabel.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-5.0],
        ]];
        if (hasValue) {
            [constraints addObjectsFromArray:@[
                [commandLabel.widthAnchor constraintEqualToConstant:104.0],
                [valueLabel.leadingAnchor constraintEqualToAnchor:commandLabel.trailingAnchor],
                [valueLabel.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
                [valueLabel.topAnchor constraintEqualToAnchor:self.topAnchor constant:4.0],
                [valueLabel.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-5.0],
            ]];
        } else {
            [constraints addObject:[commandLabel.trailingAnchor constraintEqualToAnchor:self.trailingAnchor]];
        }
        [NSLayoutConstraint activateConstraints:constraints];
    } else {
        NSMutableArray<NSLayoutConstraint *> *constraints = [NSMutableArray array];
        [constraints addObjectsFromArray:@[
            [self.heightAnchor constraintGreaterThanOrEqualToConstant:44.0],
            [marker.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
            [marker.topAnchor constraintEqualToAnchor:self.topAnchor constant:9.0],
            [marker.widthAnchor constraintEqualToConstant:18.0],
            [commandLabel.leadingAnchor constraintEqualToAnchor:marker.trailingAnchor constant:7.0],
            [commandLabel.topAnchor constraintEqualToAnchor:self.topAnchor constant:8.0],
            [detailLabel.leadingAnchor constraintEqualToAnchor:commandLabel.leadingAnchor],
            [detailLabel.trailingAnchor constraintEqualToAnchor:commandLabel.trailingAnchor],
            [detailLabel.topAnchor constraintEqualToAnchor:commandLabel.bottomAnchor constant:2.0],
            [detailLabel.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-8.0],
        ]];
        if (hasValue) {
            [constraints addObjectsFromArray:@[
                [commandLabel.widthAnchor constraintEqualToConstant:104.0],
                [valueLabel.leadingAnchor constraintEqualToAnchor:commandLabel.trailingAnchor],
                [valueLabel.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
                [valueLabel.topAnchor constraintEqualToAnchor:self.topAnchor constant:8.0],
            ]];
        } else {
            [constraints addObject:[commandLabel.trailingAnchor constraintEqualToAnchor:self.trailingAnchor]];
        }
        [NSLayoutConstraint activateConstraints:constraints];
    }
    [self addTarget:self action:@selector(commandPressed) forControlEvents:UIControlEventTouchUpInside];
    return self;
}

- (void)setHighlighted:(BOOL)highlighted
{
    [super setHighlighted:highlighted];
    if (!self.commandEnabled) return;
    [self.markerLabel.layer removeAllAnimations];

    if (!highlighted) {
        self.markerLabel.text = @"";
        self.markerLabel.textColor = UIColor.clearColor;
        self.markerLabel.alpha = 0.0;
        self.commandLabel.textColor = self.primaryCommand ? DOUTerminalGreenColor() : DOUTerminalTextColor();
        self.valueLabel.textColor = DOUTerminalTextColor();
        return;
    }

    // Show the marker immediately on touch-down. It blinks only while the
    // control remains highlighted, then disappears on release.
    self.markerLabel.text = @">";
    self.markerLabel.textColor = DOUTerminalGreenColor();
    self.markerLabel.alpha = 1.0;
    self.commandLabel.textColor = DOUTerminalGreenColor();
    self.valueLabel.textColor = DOUTerminalGreenColor();
    [UIView animateWithDuration:0.42
                          delay:0.0
                        options:UIViewAnimationOptionAutoreverse | UIViewAnimationOptionRepeat | UIViewAnimationOptionAllowUserInteraction
                     animations:^{
        self.markerLabel.alpha = 0.18;
    } completion:nil];

}

- (void)commandPressed
{
    if (self.enabled && self.handler) self.handler();
}

@end

@interface DOUTerminalView ()
@property (nonatomic, strong) NSArray<NSString *> *progressNames;
@property (nonatomic, strong) NSMutableArray<UIStackView *> *progressRows;
@property (nonatomic, strong) NSMutableArray<UILabel *> *progressDetails;
@property (nonatomic, assign) NSInteger activeProgressStage;
@end

@implementation DOUTerminalView

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (!self) return nil;
    self.translatesAutoresizingMaskIntoConstraints = NO;
    // The outer view is only a layout host. The visible terminal surface lives
    // in the tagged inner view so no rectangular background leaks outside it.
    self.backgroundColor = DOUTerminalBackgroundColor();
    self.layer.masksToBounds = NO;
    [self buildCanvas];
    return self;
}

- (void)buildCanvas
{
    UIView *surface = [[UIView alloc] init];
    surface.tag = 7100;
    surface.translatesAutoresizingMaskIntoConstraints = NO;
    surface.backgroundColor = DOUTerminalBackgroundColor();
    surface.layer.cornerRadius = 0.0;
    surface.layer.borderWidth = 0.0;
    surface.layer.borderColor = UIColor.clearColor.CGColor;
    surface.layer.masksToBounds = YES;
    [self addSubview:surface];

    UIScrollView *scrollView = [[UIScrollView alloc] init];
    scrollView.tag = 7101;
    scrollView.translatesAutoresizingMaskIntoConstraints = NO;
    scrollView.showsVerticalScrollIndicator = NO;
    scrollView.alwaysBounceVertical = YES;
    scrollView.contentInsetAdjustmentBehavior = UIScrollViewContentInsetAdjustmentNever;
    [surface addSubview:scrollView];

    UIStackView *content = [[UIStackView alloc] init];
    content.tag = 7102;
    content.translatesAutoresizingMaskIntoConstraints = NO;
    content.axis = UILayoutConstraintAxisVertical;
    content.spacing = 0.0;
    [scrollView addSubview:content];

    [NSLayoutConstraint activateConstraints:@[
        [surface.topAnchor constraintEqualToAnchor:self.topAnchor],
        [surface.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [surface.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
        [surface.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
        [scrollView.topAnchor constraintEqualToAnchor:surface.topAnchor],
        [scrollView.leadingAnchor constraintEqualToAnchor:surface.leadingAnchor],
        [scrollView.trailingAnchor constraintEqualToAnchor:surface.trailingAnchor],
        [scrollView.bottomAnchor constraintEqualToAnchor:surface.bottomAnchor],
        [content.topAnchor constraintEqualToAnchor:scrollView.contentLayoutGuide.topAnchor constant:28.0],
        [content.bottomAnchor constraintEqualToAnchor:scrollView.contentLayoutGuide.bottomAnchor constant:-30.0],
        [content.leadingAnchor constraintEqualToAnchor:scrollView.contentLayoutGuide.leadingAnchor constant:24.0],
        [content.trailingAnchor constraintEqualToAnchor:scrollView.contentLayoutGuide.trailingAnchor constant:-24.0],
        [content.widthAnchor constraintEqualToAnchor:scrollView.frameLayoutGuide.widthAnchor constant:-48.0],
    ]];
}

- (UIStackView *)contentStack
{
    return (UIStackView *)[self viewWithTag:7102];
}

- (void)clearContent
{
    UIScrollView *scrollView = (UIScrollView *)[self viewWithTag:7101];
    [scrollView setContentOffset:CGPointZero animated:NO];
    UIStackView *content = [self contentStack];
    for (UIView *view in [content.arrangedSubviews copy]) {
        [content removeArrangedSubview:view];
        [view removeFromSuperview];
    }
}

- (UILabel *)labelWithText:(NSString *)text fontSize:(CGFloat)fontSize weight:(UIFontWeight)weight color:(UIColor *)color
{
    UILabel *label = [[UILabel alloc] init];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    label.text = text;
    label.textColor = color;
    label.font = [UIFont monospacedSystemFontOfSize:fontSize weight:weight];
    label.adjustsFontForContentSizeCategory = YES;
    label.numberOfLines = 0;
    return label;
}

- (void)resetWithCommand:(NSString *)command subtitle:(NSString *)subtitle
{
    (void)command;
    [self clearContent];
    UIStackView *content = [self contentStack];

    NSString *supportValue = nil;
    for (NSString *line in [subtitle componentsSeparatedByString:@"\n"]) {
        NSUInteger split = 0;
        while (split < line.length && ![[NSCharacterSet whitespaceCharacterSet] characterIsMember:[line characterAtIndex:split]]) split++;
        NSString *key = [line substringToIndex:split];
        if ([key.lowercaseString isEqualToString:@"support"] && split < line.length) {
            supportValue = [[line substringFromIndex:split] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
            break;
        }
    }

    UIView *brandRow = [[UIView alloc] init];
    brandRow.translatesAutoresizingMaskIntoConstraints = NO;
    UILabel *brand = [self labelWithText:@"DOPAMINE" fontSize:31.0 weight:UIFontWeightBlack color:DOUTerminalTextColor()];
    brand.translatesAutoresizingMaskIntoConstraints = NO;
    UIView *brandDot = [[UIView alloc] init];
    brandDot.translatesAutoresizingMaskIntoConstraints = NO;
    brandDot.backgroundColor = DOUTerminalGreenColor();
    [brandRow addSubview:brand];
    [brandRow addSubview:brandDot];
    [NSLayoutConstraint activateConstraints:@[
        [brand.leadingAnchor constraintEqualToAnchor:brandRow.leadingAnchor],
        [brand.topAnchor constraintEqualToAnchor:brandRow.topAnchor],
        [brand.bottomAnchor constraintEqualToAnchor:brandRow.bottomAnchor],
        [brandDot.leadingAnchor constraintEqualToAnchor:brand.trailingAnchor constant:1.0],
        [brandDot.bottomAnchor constraintEqualToAnchor:brand.bottomAnchor constant:-9.0],
        [brandDot.widthAnchor constraintEqualToConstant:8.0],
        [brandDot.heightAnchor constraintEqualToConstant:8.0],
    ]];
    [content addArrangedSubview:brandRow];
    [brandRow.heightAnchor constraintGreaterThanOrEqualToConstant:40.0].active = YES;

    NSString *descriptorText = supportValue.length > 0 ? [NSString stringWithFormat:@"For %@ devices", supportValue] : @"For supported iOS devices";
    UILabel *descriptor = [self labelWithText:descriptorText fontSize:13.0 weight:UIFontWeightRegular color:DOUTerminalMutedColor()];
    descriptor.numberOfLines = 1;
    descriptor.lineBreakMode = NSLineBreakByClipping;
    [content addArrangedSubview:descriptor];

    UIView *rule = [[UIView alloc] init];
    rule.translatesAutoresizingMaskIntoConstraints = NO;
    rule.backgroundColor = DOUTerminalRuleColor();
    [content addArrangedSubview:rule];
    [rule.heightAnchor constraintEqualToConstant:1.0].active = YES;

    if (subtitle.length > 0) {
        for (NSString *line in [subtitle componentsSeparatedByString:@"\n"]) {
            if (!line.length) continue;
            NSUInteger split = 0;
            while (split < line.length && ![[NSCharacterSet whitespaceCharacterSet] characterIsMember:[line characterAtIndex:split]]) split++;
            NSString *rawKey = [line substringToIndex:split];
            if ([rawKey.lowercaseString isEqualToString:@"support"]) continue;
            NSString *key = DOUTerminalDisplayKey(rawKey);
            NSString *value = split < line.length ? [[line substringFromIndex:split] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]] : @"";
            [self appendSystemRowWithKey:key value:value];
        }
    }
    UIView *bottomRule = [[UIView alloc] init];
    bottomRule.translatesAutoresizingMaskIntoConstraints = NO;
    bottomRule.backgroundColor = DOUTerminalRuleColor();
    [content addArrangedSubview:bottomRule];
    [bottomRule.heightAnchor constraintEqualToConstant:1.0].active = YES;
    [self appendBlankLine];
}

- (void)appendPrompt:(NSString *)prompt
{
    // Section prompts were part of the old shell mock-up. Terminal pages now
    // render only the actual content and commands, so never emit `$ ...` rows.
    (void)prompt;
}

- (void)appendLine:(NSString *)line color:(UIColor *)color
{
    UIStackView *content = [self contentStack];
    UILabel *label = [self labelWithText:line fontSize:12.0 weight:UIFontWeightRegular color:color ?: DOUTerminalTextColor()];
    [content addArrangedSubview:label];
}

- (void)appendSystemRowWithKey:(NSString *)key value:(NSString *)value
{
    UIStackView *content = [self contentStack];
    UIStackView *row = [[UIStackView alloc] init];
    row.translatesAutoresizingMaskIntoConstraints = NO;
    row.axis = UILayoutConstraintAxisHorizontal;
    row.alignment = UIStackViewAlignmentFirstBaseline;
    row.spacing = 0.0;

    UIFont *font = [UIFont monospacedSystemFontOfSize:12.0 weight:UIFontWeightRegular];
    UILabel *keyLabel = [self labelWithText:key fontSize:12.0 weight:UIFontWeightRegular color:DOUTerminalGreenColor()];
    UILabel *valueLabel = [self labelWithText:value fontSize:12.0 weight:UIFontWeightRegular color:DOUTerminalTextColor()];
    keyLabel.numberOfLines = 1;
    valueLabel.numberOfLines = 1;
    valueLabel.lineBreakMode = NSLineBreakByClipping;
    [row addArrangedSubview:keyLabel];
    [row addArrangedSubview:valueLabel];
    [keyLabel.widthAnchor constraintEqualToConstant:84.0].active = YES;
    [content addArrangedSubview:row];
}

- (void)appendAttributedLine:(NSAttributedString *)line
{
    UIStackView *content = [self contentStack];
    UILabel *label = [[UILabel alloc] init];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    label.attributedText = line;
    label.numberOfLines = 0;
    label.adjustsFontForContentSizeCategory = YES;
    [content addArrangedSubview:label];
}

- (void)appendBlankLine
{
    UIStackView *content = [self contentStack];
    UIView *spacer = [[UIView alloc] init];
    spacer.translatesAutoresizingMaskIntoConstraints = NO;
    [content addArrangedSubview:spacer];
    [spacer.heightAnchor constraintEqualToConstant:10.0].active = YES;
}

- (void)appendCommand:(NSString *)command detail:(NSString *)detail enabled:(BOOL)enabled handler:(void (^)(void))handler
{
    UIStackView *content = [self contentStack];
    DOUTerminalCommandView *row;
    if (self.compactCommandRows) {
        row = [[DOUTerminalCommandView alloc] initWithCommand:command detail:nil enabled:enabled compact:YES handler:handler];
    } else {
        row = [[DOUTerminalCommandView alloc] initWithCommand:command detail:detail enabled:enabled handler:handler];
    }
    [content addArrangedSubview:row];
}

- (void)appendCompactCommand:(NSString *)command enabled:(BOOL)enabled handler:(void (^)(void))handler
{
    UIStackView *content = [self contentStack];
    DOUTerminalCommandView *row = [[DOUTerminalCommandView alloc] initWithCommand:command detail:nil enabled:enabled compact:YES handler:handler];
    [content addArrangedSubview:row];
}

- (void)appendView:(UIView *)view
{
    if (!view) return;
    UIStackView *content = [self contentStack];
    view.translatesAutoresizingMaskIntoConstraints = NO;
    [content addArrangedSubview:view];
}

- (void)appendBackCommandWithHandler:(void (^)(void))handler
{
    [self appendBlankLine];
    [self appendCommand:@"back" detail:@"return to previous page" enabled:YES handler:handler];
}

- (void)beginProgress
{
    [self clearContent];
    [self resetWithCommand:@"" subtitle:nil];
    self.progressNames = @[
        @"target confirmation", @"kernelcache acquisition", @"kernelcache layout analysis", @"kernel access acquisition",
        @"privilege escalation", @"bootstrap preparation", @"exploit cleanup", @"trust cache injection",
        @"environment initialization", @"RootHide mount", @"package preparation", @"jailbreak finalization"
    ];
    self.progressRows = [NSMutableArray arrayWithCapacity:self.progressNames.count];
    self.progressDetails = [NSMutableArray arrayWithCapacity:self.progressNames.count];
    self.activeProgressStage = 1;

    [self appendBlankLine];
    UIStackView *content = [self contentStack];
    for (NSUInteger index = 0; index < self.progressNames.count; index++) {
        UIStackView *row = [[UIStackView alloc] init];
        row.axis = UILayoutConstraintAxisVertical;
        row.spacing = 1.0;
        row.translatesAutoresizingMaskIntoConstraints = NO;

        UILabel *headline = [self labelWithText:[NSString stringWithFormat:@"· %02lu/12 %@", (unsigned long)index + 1, DOUTerminalDisplayCommand(self.progressNames[index])] fontSize:12.0 weight:UIFontWeightMedium color:DOUTerminalMutedColor()];
        UILabel *detail = [self labelWithText:@"" fontSize:10.0 weight:UIFontWeightRegular color:DOUTerminalMutedColor()];
        detail.hidden = index != 0;
        detail.numberOfLines = 3;
        [row addArrangedSubview:headline];
        [row addArrangedSubview:detail];
        [content addArrangedSubview:row];
        [self.progressRows addObject:row];
        [self.progressDetails addObject:detail];
    }
    [self updateProgressToStage:1 detail:nil];
}

- (NSInteger)progressStageForLog:(NSString *)log
{
    NSString *value = log.lowercaseString;
    if ([value containsString:@"target"] || [value containsString:@"confirm"] || [value containsString:@"supported"]) return 1;
    if ([value containsString:@"kernelcache"] || [value containsString:@"downloading kernel"] || [value containsString:@"launching kexploitd"]) return 2;
    if ([value containsString:@"patchfinding"] || [value containsString:@"kernelcache layout"] || [value containsString:@"kernel layout"]) return 3;
    if ([value containsString:@"exploiting kernel"] || [value containsString:@"kernel exploit"] || [value containsString:@"kernel access"] || [value containsString:@"launching oobpci"]) return 4;
    if ([value containsString:@"bypassing pac"] || [value containsString:@"pac bypass"] || [value containsString:@"bypassing ppl"] || [value containsString:@"ppl bypass"] || [value containsString:@"elevating"] || [value containsString:@"privilege"]) return 5;
    if ([value containsString:@"phys r/w"] || [value containsString:@"gaining r/w"] || [value containsString:@"primitive"]) return 6;
    if ([value containsString:@"cleaning up"] || [value containsString:@"cleanup"]) return 7;
    if ([value containsString:@"trustcache"] || [value containsString:@"trust cache"]) return 8;
    if ([value containsString:@"initializing environment"] || [value containsString:@"initializing protection"]) return 9;
    if ([value containsString:@"bind mount"] || [value containsString:@"roothide"] || [value containsString:@"mount"]) return 10;
    if ([value containsString:@"extracting bootstrap"] || [value containsString:@"updating basebin"] || [value containsString:@"updating symlinks"] || [value containsString:@"bundled packages"] || [value containsString:@"duplicate apps"]) return 11;
    if ([value containsString:@"finalizing"] || [value containsString:@"rebooting userspace"]) return 12;
    return 0;
}

- (void)updateProgressToStage:(NSInteger)stage detail:(NSString *)detail
{
    if (self.progressRows.count == 0) [self beginProgress];
    NSInteger target = MIN(MAX(stage, 1), (NSInteger)self.progressRows.count);
    if (target < self.activeProgressStage) target = self.activeProgressStage;
    self.activeProgressStage = target;

    for (NSInteger index = 0; index < self.progressRows.count; index++) {
        UILabel *headline = (UILabel *)self.progressRows[index].arrangedSubviews.firstObject;
        UILabel *detailLabel = self.progressDetails[index];
        NSString *name = self.progressNames[index];
        if (index < target - 1) {
            headline.text = [NSString stringWithFormat:@"✓ %02ld/12 %@", (long)index + 1, DOUTerminalDisplayCommand(name)];
            headline.textColor = DOUTerminalGreenColor();
            detailLabel.text = @"";
            detailLabel.hidden = YES;
        } else if (index == target - 1) {
            headline.text = [NSString stringWithFormat:@"● %02ld/12 %@", (long)index + 1, DOUTerminalDisplayCommand(name)];
            headline.textColor = DOUTerminalGreenColor();
            detailLabel.hidden = NO;
        } else {
            headline.text = [NSString stringWithFormat:@"· %02ld/12 %@", (long)index + 1, DOUTerminalDisplayCommand(name)];
            headline.textColor = DOUTerminalMutedColor();
            detailLabel.text = @"";
            detailLabel.hidden = YES;
        }
    }

    if (detail.length && target > 0) {
        UILabel *detailLabel = self.progressDetails[target - 1];
        NSMutableArray<NSString *> *lines = [NSMutableArray arrayWithArray:[detailLabel.text componentsSeparatedByString:@"\n"] ?: @[]];
        if (lines.count == 1 && lines.firstObject.length == 0) [lines removeAllObjects];
        [lines addObject:[NSString stringWithFormat:@"└─ %@", detail]];
        while (lines.count > 3) [lines removeObjectAtIndex:0];
        detailLabel.text = [lines componentsJoinedByString:@"\n"];
    }
}

#pragma mark - DOLogViewProtocol

- (void)showLog:(NSString *)log
{
    if (!log.length) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self.progressRows.count > 0) {
            NSInteger stage = [self progressStageForLog:log];
            [self updateProgressToStage:(stage > 0 ? stage : self.activeProgressStage) detail:log];
            UIScrollView *scrollView = (UIScrollView *)[self viewWithTag:7101];
            [scrollView layoutIfNeeded];
            CGFloat bottom = MAX(-scrollView.adjustedContentInset.top, scrollView.contentSize.height - scrollView.bounds.size.height + scrollView.adjustedContentInset.bottom);
            [scrollView setContentOffset:CGPointMake(0, bottom) animated:NO];
        } else {
            [self appendLine:log color:DOUTerminalMutedColor()];
        }
    });
}

- (void)updateLog:(NSString *)log
{
    [self showLog:log];
}

- (void)didComplete
{
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self.progressRows.count > 0) {
            [self updateProgressToStage:self.progressRows.count detail:nil];
            UILabel *detailLabel = self.progressDetails.lastObject;
            detailLabel.text = @"";
            detailLabel.hidden = YES;
            self.activeProgressStage = 0;
        } else {
            [self appendLine:@"done" color:DOUTerminalGreenColor()];
        }
    });
}

@end

@implementation DOGlobalAppearance

+ (UIColor *)terminalBackgroundColor
{
    return [UIColor colorWithRed:0.985 green:0.982 blue:0.975 alpha:1.0];
}

+ (UIColor *)terminalTextColor
{
    return [UIColor colorWithRed:0.08 green:0.08 blue:0.08 alpha:1.0];
}

+ (UIColor *)terminalMutedColor
{
    return [UIColor colorWithRed:0.38 green:0.38 blue:0.38 alpha:1.0];
}

+ (UIColor *)terminalAccentColor
{
    return [UIColor colorWithRed:0.25 green:0.88 blue:0.34 alpha:1.0];
}

+ (UIColor *)terminalRuleColor
{
    return [UIColor colorWithRed:0.18 green:0.18 blue:0.18 alpha:0.38];
}

+ (UIImageSymbolConfiguration *)smallIconImageConfiguration
{
    return [UIImageSymbolConfiguration configurationWithPointSize: 14 weight:UIImageSymbolWeightMedium];
}

+ (UIButtonConfiguration *)defaultButtonConfiguration
{
    UIButtonConfiguration *configuration = [UIButtonConfiguration plainButtonConfiguration];
    configuration.imagePadding = 10;
    configuration.baseForegroundColor = [UIColor whiteColor];
#ifdef NSLineBreakByClipping
    configuration.titleLineBreakMode = NSLineBreakByClipping;
#endif

    // IN DARK MODE, APPLE JUST ADDS WHITE WHEN A BUTTON IS HIGHLIGHTED WHEN IT'S SET UP VIA UIButtonConfiguration
    // UNFORTUNATELY THEY FORGOT ABOUT THE POSSIBILITY ABOUT THERE BEING A WHITE BUTTON, SO THOSE JUST DON'T SHOW ANY HIGHLIGHT COLOR
    // HACKY WORKAROUND TO FIX FIX THIS MESS; SCREW APPLE
    configuration.titleTextAttributesTransformer = ^NSDictionary<NSAttributedStringKey,id> * _Nonnull(NSDictionary<NSAttributedStringKey,id> * _Nonnull textAttributes) {
        // Something makes me think the person that developed this API never actually used it...
        // OR ELSE WHERE IS MY BUTTON REFERENCE TO KNOW WHAT STATE I'M EVEN DEALING WITH???
        // THIS IS A SUPER HACKY WAY OF DETERMINING WHETHER THE BUTTON IS HIGHLIGHTED OR NOT
        // WHEN IT'S HIGHLIGHTED THE COLOR WILL BE IN UIExtendedSRGBColorSpace
        // WHEN NOT HIGHLIGHTED IT WILL BE IN UIExtendedGrayColorSpace
        // WHEN NOT IN DARK MODE IT WILL ALREADY BE WHAT WE WANT, JUST SKIP
        NSMutableDictionary<NSAttributedStringKey,id> *textAttributesM = textAttributes.mutableCopy;
        UIColor *foregroundColor = textAttributes[NSForegroundColorAttributeName];
        CGFloat alpha, white;
        [foregroundColor getWhite:&white alpha:&alpha];
        if ((int)white == 1 && (int)alpha == 1) {
            CGColorSpaceRef colorSpace = CGColorGetColorSpace(foregroundColor.CGColor);
            CGColorSpaceModel model = CGColorSpaceGetModel(colorSpace);
            if (model == kCGColorSpaceModelRGB) {
                textAttributesM[NSForegroundColorAttributeName] = [[UIColor whiteColor] colorWithAlphaComponent:0.75];
            }
        }
        // textAttributesM[NSFontAttributeName] = [UIFont systemFontOfSize:16 weight:UIFontWeightMedium];
        return textAttributesM;
    };
    configuration.imageColorTransformer = ^UIColor * _Nonnull(UIColor * _Nonnull color) {
        // Something makes me think the person that developed this API never actually used it...
        // OR ELSE WHERE IS MY BUTTON REFERENCE TO KNOW WHAT STATE I'M EVEN DEALING WITH???
        // THIS IS A SUPER HACKY WAY OF DETERMINING WHETHER THE BUTTON IS HIGHLIGHTED OR NOT
        // WHEN IT'S HIGHLIGHTED THE COLOR WILL BE IN UIExtendedSRGBColorSpace
        // WHEN NOT HIGHLIGHTED IT WILL BE IN UIExtendedGrayColorSpace
        // WHEN NOT IN DARK MODE IT WILL ALREADY BE WHAT WE WANT, JUST SKIP
        CGFloat alpha, white;
        [color getWhite:&white alpha:&alpha];
        if ((int)white == 1 && (int)alpha == 1) {
            CGColorSpaceRef colorSpace = CGColorGetColorSpace(color.CGColor);
            CGColorSpaceModel model = CGColorSpaceGetModel(colorSpace);
            if (model == kCGColorSpaceModelRGB) {
                return [color colorWithAlphaComponent:0.75];
            }
        }
        return color;
    };
    
    return configuration;
}

+ (UIButtonConfiguration *)defaultButtonConfigurationWithImagePadding:(CGFloat)imagePadding
{
    UIButtonConfiguration *configuration = [DOGlobalAppearance defaultButtonConfiguration];
    configuration.imagePadding = imagePadding;
    return configuration;
}

#pragma mark - Attributed Strings

+ (NSAttributedString*)mainSubtitleString:(NSString*)string
{
    return [[NSAttributedString alloc] initWithString:string attributes:@{
        NSFontAttributeName: [UIFont systemFontOfSize:14 weight:UIFontWeightMedium],
        NSForegroundColorAttributeName: [UIColor whiteColor],
    }];
}

+ (NSAttributedString*)secondarySubtitleString:(NSString*)string
{
    return [[NSAttributedString alloc] initWithString:string attributes:@{
        NSFontAttributeName: [UIFont systemFontOfSize:14 weight:UIFontWeightRegular],
        NSForegroundColorAttributeName: [UIColor colorWithWhite:1 alpha:0.60],
    }];
}

+ (NSAttributedString*)terminalSubtitleString:(NSString*)string
{
    NSMutableParagraphStyle *paragraphStyle = [[NSMutableParagraphStyle alloc] init];
    paragraphStyle.lineSpacing = 3.0;
    paragraphStyle.paragraphSpacing = 2.0;

    NSDictionary *baseAttributes = @{
        NSFontAttributeName: [UIFont monospacedSystemFontOfSize:13 weight:UIFontWeightMedium],
        NSForegroundColorAttributeName: [UIColor colorWithWhite:1.0 alpha:0.86],
        NSParagraphStyleAttributeName: paragraphStyle,
    };
    NSMutableAttributedString *result = [[NSMutableAttributedString alloc] initWithString:string attributes:baseAttributes];
    UIColor *labelColor = [UIColor colorWithRed:0.30 green:0.95 blue:0.48 alpha:1.0];
    NSArray<NSString *> *lines = [string componentsSeparatedByString:@"\n"];
    NSUInteger offset = 0;
    for (NSUInteger index = 0; index < lines.count; index++) {
        NSString *line = lines[index];
        if (index > 0) {
            NSRange separator = [line rangeOfString:@"  "];
            if (separator.location != NSNotFound && separator.location > 0) {
                [result addAttribute:NSForegroundColorAttributeName value:labelColor range:NSMakeRange(offset, separator.location)];
            }
        }
        offset += line.length + 1;
    }
    return result;
}

+ (BOOL)isHomeButtonDevice
{
   return [[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPhone && [[UIApplication sharedApplication] keyWindow].safeAreaInsets.bottom == 0;
}

+ (BOOL)isRTL
{
    return [UIApplication sharedApplication].userInterfaceLayoutDirection == UIUserInterfaceLayoutDirectionRightToLeft;
}

+ (BOOL)isSmallDevice
{
    UIWindow *window = [[UIApplication sharedApplication] keyWindow];
    return window.frame.size.height < SE_PHONE_SIZE_CONST + 50;
}

@end
