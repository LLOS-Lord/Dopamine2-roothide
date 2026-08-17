//
//  DOButtonCell.m
//  Dopamine
//

#import "DOButtonCell.h"
#import "DOActionMenuButton.h"
#import "DOGlobalAppearance.h"
#import "DOUIManager.h"

@implementation DOButtonCell

- (id)initWithStyle:(int)arg1 reuseIdentifier:(id)arg2 specifier:(PSSpecifier *)specifier
{
    self = [super init];
    if (!self) return nil;

    self.backgroundColor = UIColor.clearColor;
    self.contentView.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.045];

    UIAction *action = [UIAction actionWithTitle:DOLocalizedString([specifier propertyForKey:@"title"])
                                            image:nil
                                       identifier:[specifier propertyForKey:@"key"]
                                          handler:^(__kindof UIAction *action) {
        SEL selector = NSSelectorFromString([specifier propertyForKey:@"action"]);
        if ([[specifier target] respondsToSelector:selector]) {
            [[specifier target] performSelector:selector withObject:specifier];
        }
    }];

    DOActionMenuButton *button = [DOActionMenuButton buttonWithAction:action chevron:NO];
    UIButtonConfiguration *configuration = button.configuration;
    configuration.image = nil;
    configuration.imagePadding = 0;
    configuration.contentInsets = NSDirectionalEdgeInsetsMake(0, 16, 0, 16);
    configuration.baseForegroundColor = UIColor.whiteColor;
    button.configuration = configuration;
    button.titleLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    button.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
    button.translatesAutoresizingMaskIntoConstraints = NO;

    [self.contentView addSubview:button];
    [NSLayoutConstraint activateConstraints:@[
        [button.leadingAnchor constraintEqualToAnchor:self.contentView.leadingAnchor],
        [button.trailingAnchor constraintEqualToAnchor:self.contentView.trailingAnchor],
        [button.topAnchor constraintEqualToAnchor:self.contentView.topAnchor],
        [button.bottomAnchor constraintEqualToAnchor:self.contentView.bottomAnchor],
    ]];
    return self;
}

@end
