//
//  DOHeaderCell.m
//  Dopamine
//

#import "DOHeaderCell.h"

@implementation DOHeaderCell

- (id)initWithSpecifier:(PSSpecifier *)specifier
{
    if (self = [super init]) {
        self.backgroundColor = UIColor.clearColor;
        self.contentView.backgroundColor = UIColor.clearColor;

        UILabel *eyebrow = [[UILabel alloc] init];
        eyebrow.text = @"DOPAMINE ROOT HIDE";
        eyebrow.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightSemibold];
        eyebrow.textColor = [UIColor colorWithRed:0.30 green:0.90 blue:0.54 alpha:0.95];
        eyebrow.translatesAutoresizingMaskIntoConstraints = NO;
        [self.contentView addSubview:eyebrow];

        UILabel *title = [[UILabel alloc] init];
        title.text = [specifier propertyForKey:@"title"] ?: @"Settings";
        title.font = [UIFont preferredFontForTextStyle:UIFontTextStyleLargeTitle];
        title.adjustsFontForContentSizeCategory = YES;
        title.textColor = UIColor.whiteColor;
        title.translatesAutoresizingMaskIntoConstraints = NO;
        [self.contentView addSubview:title];

        UIView *rule = [[UIView alloc] init];
        rule.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.16];
        rule.translatesAutoresizingMaskIntoConstraints = NO;
        [self.contentView addSubview:rule];

        [NSLayoutConstraint activateConstraints:@[
            [eyebrow.leadingAnchor constraintEqualToAnchor:self.contentView.leadingAnchor constant:16],
            [eyebrow.trailingAnchor constraintEqualToAnchor:self.contentView.trailingAnchor constant:-16],
            [eyebrow.topAnchor constraintEqualToAnchor:self.contentView.topAnchor constant:8],
            [title.leadingAnchor constraintEqualToAnchor:eyebrow.leadingAnchor],
            [title.trailingAnchor constraintEqualToAnchor:eyebrow.trailingAnchor],
            [title.topAnchor constraintEqualToAnchor:eyebrow.bottomAnchor constant:3],
            [rule.leadingAnchor constraintEqualToAnchor:eyebrow.leadingAnchor],
            [rule.trailingAnchor constraintEqualToAnchor:eyebrow.trailingAnchor],
            [rule.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:10],
            [rule.heightAnchor constraintEqualToConstant:1],
            [rule.bottomAnchor constraintEqualToAnchor:self.contentView.bottomAnchor constant:-8],
        ]];
    }
    return self;
}

- (CGFloat)preferredHeightForWidth:(CGFloat)width
{
    return 104.0;
}

@end
