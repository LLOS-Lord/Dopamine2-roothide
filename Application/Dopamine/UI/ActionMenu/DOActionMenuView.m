//
//  DOActionMenuView.m
//  Dopamine
//

#import "DOActionMenuView.h"
#import "DOActionMenuButton.h"
#import "DOGlobalAppearance.h"

@implementation DOActionMenuView

- (instancetype)initWithActions:(NSArray<UIAction *> *)actions delegate:(id<DOActionMenuDelegate>)delegate
{
    self = [super initWithFrame:CGRectZero];
    if (!self) return nil;

    self.delegate = delegate;
    self.actions = actions;
    self.translatesAutoresizingMaskIntoConstraints = NO;
    self.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.045];
    self.layer.cornerRadius = 24.0;
    self.layer.cornerCurve = kCACornerCurveContinuous;
    self.layer.borderWidth = 1.0;
    self.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.09].CGColor;
    self.layer.masksToBounds = YES;
    [self refreshStack];
    return self;
}

- (void)setActions:(NSArray<UIAction *> *)actions
{
    _actions = [actions copy];
    if (self.window || self.superview) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self refreshStack];
        });
    }
}

- (void)refreshStack
{
    for (UIView *subview in self.subviews) {
        [subview removeFromSuperview];
    }

    UILabel *sectionLabel = [[UILabel alloc] init];
    sectionLabel.translatesAutoresizingMaskIntoConstraints = NO;
    sectionLabel.text = @"QUICK ACTIONS";
    sectionLabel.textColor = [UIColor colorWithRed:0.30 green:0.90 blue:0.54 alpha:0.95];
    sectionLabel.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightSemibold];
    sectionLabel.adjustsFontForContentSizeCategory = YES;
    [self addSubview:sectionLabel];

    UIStackView *buttonStack = [[UIStackView alloc] init];
    buttonStack.translatesAutoresizingMaskIntoConstraints = NO;
    buttonStack.axis = UILayoutConstraintAxisVertical;
    buttonStack.spacing = 0.0;
    [self addSubview:buttonStack];

    CGFloat buttonHeight = [DOGlobalAppearance isSmallDevice] ? 52.0 : 58.0;
    for (NSUInteger index = 0; index < self.actions.count; index++) {
        UIAction *action = self.actions[index];
        DOActionMenuButton *button = [DOActionMenuButton buttonWithAction:action chevron:(index == 0 || [self.delegate actionMenuShowsChevronForAction:action])];
        button.enabled = [self.delegate actionMenuActionIsEnabled:action];
        button.backgroundColor = UIColor.clearColor;
        [button setBottomSeparator:index + 1 < self.actions.count];
        [buttonStack addArrangedSubview:button];
        [button.heightAnchor constraintEqualToConstant:buttonHeight].active = YES;
    }

    [NSLayoutConstraint activateConstraints:@[
        [sectionLabel.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:20.0],
        [sectionLabel.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-20.0],
        [sectionLabel.topAnchor constraintEqualToAnchor:self.topAnchor constant:18.0],
        [sectionLabel.heightAnchor constraintEqualToConstant:16.0],
        [buttonStack.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:16.0],
        [buttonStack.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-16.0],
        [buttonStack.topAnchor constraintEqualToAnchor:sectionLabel.bottomAnchor constant:8.0],
        [buttonStack.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-12.0],
    ]];
}

- (void)hide
{
    [self setUserInteractionEnabled:NO];
    CGAffineTransform transform = CGAffineTransformMakeTranslation(0, -75);
    transform = CGAffineTransformScale(transform, 0.6, 0.6);
    [UIView animateWithDuration:0.3 delay:0.0 usingSpringWithDamping:0.9 initialSpringVelocity:2.0 options:UIViewAnimationOptionCurveEaseInOut animations:^{
        self.alpha = 0.0;
        self.transform = transform;
    } completion:nil];
}

@end
