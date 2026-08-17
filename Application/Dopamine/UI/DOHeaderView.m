//
//  DOHeaderView.m
//  Dopamine
//

#import "DOHeaderView.h"

static UIColor *DOHeroAccentColor(void)
{
    return [UIColor colorWithRed:0.30 green:0.95 blue:0.48 alpha:1.0];
}

static UIColor *DOHeroSurfaceColor(void)
{
    return [UIColor colorWithWhite:1.0 alpha:0.055];
}

@implementation DOHeaderView

- (instancetype)initWithImage:(UIImage *)image subtitles:(NSArray<NSAttributedString *> *)subtitles
{
    self = [super initWithFrame:CGRectZero];
    if (!self) return nil;

    (void)image;
    self.translatesAutoresizingMaskIntoConstraints = NO;
    self.backgroundColor = UIColor.clearColor;

    UIView *hero = [[UIView alloc] init];
    hero.translatesAutoresizingMaskIntoConstraints = NO;
    hero.backgroundColor = DOHeroSurfaceColor();
    hero.layer.cornerRadius = 28.0;
    hero.layer.cornerCurve = kCACornerCurveContinuous;
    hero.layer.borderWidth = 1.0;
    hero.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.10].CGColor;
    hero.layer.masksToBounds = YES;
    [self addSubview:hero];

    UIView *accentBar = [[UIView alloc] init];
    accentBar.translatesAutoresizingMaskIntoConstraints = NO;
    accentBar.backgroundColor = DOHeroAccentColor();
    accentBar.layer.cornerRadius = 2.0;
    [hero addSubview:accentBar];

    UILabel *eyebrow = [[UILabel alloc] init];
    eyebrow.translatesAutoresizingMaskIntoConstraints = NO;
    eyebrow.text = @"DOPAMINE  /  ROOT HIDE";
    eyebrow.textColor = DOHeroAccentColor();
    eyebrow.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightSemibold];
    eyebrow.adjustsFontForContentSizeCategory = YES;
    [hero addSubview:eyebrow];

    UILabel *title = [[UILabel alloc] init];
    title.translatesAutoresizingMaskIntoConstraints = NO;
    title.text = @"Dopamine";
    title.textColor = UIColor.whiteColor;
    title.font = [UIFont systemFontOfSize:42 weight:UIFontWeightHeavy];
    title.adjustsFontForContentSizeCategory = YES;
    title.adjustsFontSizeToFitWidth = YES;
    title.minimumScaleFactor = 0.72;
    [hero addSubview:title];

    UILabel *descriptor = [[UILabel alloc] init];
    descriptor.translatesAutoresizingMaskIntoConstraints = NO;
    descriptor.text = @"A focused RootHide system utility";
    descriptor.textColor = [UIColor colorWithWhite:1.0 alpha:0.58];
    descriptor.font = [UIFont preferredFontForTextStyle:UIFontTextStyleSubheadline];
    descriptor.adjustsFontForContentSizeCategory = YES;
    [hero addSubview:descriptor];

    UIView *rule = [[UIView alloc] init];
    rule.translatesAutoresizingMaskIntoConstraints = NO;
    rule.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.12];
    [hero addSubview:rule];

    UIView *snapshot = [[UIView alloc] init];
    snapshot.translatesAutoresizingMaskIntoConstraints = NO;
    snapshot.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.16];
    snapshot.layer.cornerRadius = 17.0;
    snapshot.layer.cornerCurve = kCACornerCurveContinuous;
    [hero addSubview:snapshot];

    UIStackView *snapshotStack = [[UIStackView alloc] init];
    snapshotStack.translatesAutoresizingMaskIntoConstraints = NO;
    snapshotStack.axis = UILayoutConstraintAxisVertical;
    snapshotStack.spacing = 8.0;
    [snapshot addSubview:snapshotStack];

    for (NSAttributedString *subtitle in subtitles) {
        UILabel *label = [[UILabel alloc] init];
        label.translatesAutoresizingMaskIntoConstraints = NO;
        label.attributedText = subtitle;
        label.numberOfLines = 0;
        label.lineBreakMode = NSLineBreakByWordWrapping;
        label.adjustsFontForContentSizeCategory = YES;
        [snapshotStack addArrangedSubview:label];
    }

    UILabel *footer = [[UILabel alloc] init];
    footer.translatesAutoresizingMaskIntoConstraints = NO;
    footer.text = @"LLOS Lord  •  system utility";
    footer.textColor = [UIColor colorWithWhite:1.0 alpha:0.38];
    footer.font = [UIFont monospacedSystemFontOfSize:10 weight:UIFontWeightRegular];
    footer.adjustsFontForContentSizeCategory = YES;
    [hero addSubview:footer];

    [NSLayoutConstraint activateConstraints:@[
        [hero.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [hero.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
        [hero.topAnchor constraintEqualToAnchor:self.topAnchor],
        [hero.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
        [accentBar.leadingAnchor constraintEqualToAnchor:hero.leadingAnchor],
        [accentBar.topAnchor constraintEqualToAnchor:hero.topAnchor],
        [accentBar.bottomAnchor constraintEqualToAnchor:hero.bottomAnchor],
        [accentBar.widthAnchor constraintEqualToConstant:4.0],
        [eyebrow.leadingAnchor constraintEqualToAnchor:hero.leadingAnchor constant:24.0],
        [eyebrow.trailingAnchor constraintLessThanOrEqualToAnchor:hero.trailingAnchor constant:-20.0],
        [eyebrow.topAnchor constraintEqualToAnchor:hero.topAnchor constant:24.0],
        [title.leadingAnchor constraintEqualToAnchor:eyebrow.leadingAnchor],
        [title.trailingAnchor constraintEqualToAnchor:hero.trailingAnchor constant:-20.0],
        [title.topAnchor constraintEqualToAnchor:eyebrow.bottomAnchor constant:8.0],
        [descriptor.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [descriptor.trailingAnchor constraintEqualToAnchor:title.trailingAnchor],
        [descriptor.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:4.0],
        [rule.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [rule.trailingAnchor constraintEqualToAnchor:title.trailingAnchor],
        [rule.topAnchor constraintEqualToAnchor:descriptor.bottomAnchor constant:18.0],
        [rule.heightAnchor constraintEqualToConstant:1.0],
        [snapshot.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [snapshot.trailingAnchor constraintEqualToAnchor:title.trailingAnchor],
        [snapshot.topAnchor constraintEqualToAnchor:rule.bottomAnchor constant:16.0],
        [snapshotStack.leadingAnchor constraintEqualToAnchor:snapshot.leadingAnchor constant:16.0],
        [snapshotStack.trailingAnchor constraintEqualToAnchor:snapshot.trailingAnchor constant:-16.0],
        [snapshotStack.topAnchor constraintEqualToAnchor:snapshot.topAnchor constant:14.0],
        [snapshotStack.bottomAnchor constraintEqualToAnchor:snapshot.bottomAnchor constant:-14.0],
        [footer.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [footer.trailingAnchor constraintEqualToAnchor:title.trailingAnchor],
        [footer.topAnchor constraintEqualToAnchor:snapshot.bottomAnchor constant:14.0],
        [footer.bottomAnchor constraintEqualToAnchor:hero.bottomAnchor constant:-22.0],
    ]];

    return self;
}

@end
