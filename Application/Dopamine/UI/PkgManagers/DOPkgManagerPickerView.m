//
//  DOPkgManagerPickerView.m
//  Dopamine
//

#import "DOPkgManagerPickerView.h"
#import "DOUIManager.h"
#import "DOGlobalAppearance.h"

@interface DOPkgManagerPickerView ()
@property (nonatomic, copy) void (^callback)(BOOL success);
@property (nonatomic, strong) DOUTerminalView *terminalView;
@end

@implementation DOPkgManagerPickerView

- (instancetype)initWithCallback:(void (^)(BOOL))callback
{
    self = [super initWithFrame:CGRectZero];
    if (!self) return nil;
    self.callback = callback;
    self.backgroundColor = [DOGlobalAppearance terminalBackgroundColor];
    [self rebuildTerminalPage];
    return self;
}

- (void)rebuildTerminalPage
{
    [self.terminalView removeFromSuperview];
    self.terminalView = [[DOUTerminalView alloc] initWithFrame:CGRectZero];
    self.terminalView.compactCommandRows = YES;
    self.terminalView.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:self.terminalView];
    [NSLayoutConstraint activateConstraints:@[
        [self.terminalView.topAnchor constraintEqualToAnchor:self.topAnchor constant:8.0],
        [self.terminalView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:18.0],
        [self.terminalView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-18.0],
        [self.terminalView.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-8.0],
    ]];

    NSArray *packageManagers = [DOUIManager sharedInstance].availablePackageManagers ?: @[];
    NSArray *enabled = [DOUIManager sharedInstance].enabledPackageManagerKeys ?: @[];
    [self.terminalView resetWithCommand:@"Package Managers" subtitle:nil];
    for (NSDictionary *manager in packageManagers) {
        NSString *key = manager[@"Key"] ?: @"";
        NSString *name = manager[@"Display Name"] ?: key;
        BOOL isEnabled = [enabled containsObject:key];
        [self.terminalView appendCommand:name detail:(isEnabled ? @"On" : @"Off") enabled:YES handler:^{
            [[DOUIManager sharedInstance] setPackageManager:key enabled:!isEnabled];
            [self rebuildTerminalPage];
        }];
    }
    [self.terminalView appendBlankLine];
    [self.terminalView appendCommand:@"Continue" detail:nil enabled:(enabled.count > 0) handler:^{
        if (self.callback) self.callback(YES);
    }];
}

@end
