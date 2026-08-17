//
//  DOLogCrashViewController.m
//  Dopamine
//

#import "DOLogCrashViewController.h"
#import "DOGlobalAppearance.h"
#import "DOUIManager.h"

@interface DOLogCrashViewController ()
@property (nonatomic, copy) NSString *errorTitle;
@property (nonatomic, copy) void (^returnHandler)(void);
@property (nonatomic, strong) DOUTerminalView *terminalView;
@end

@implementation DOLogCrashViewController

- (instancetype)initWithTitle:(NSString *)title
{
    return [self initWithTitle:title returnHandler:nil];
}

- (instancetype)initWithTitle:(NSString *)title returnHandler:(void (^)(void))returnHandler
{
    self = [super init];
    if (self) {
        self.errorTitle = title;
        self.returnHandler = [returnHandler copy];
    }
    return self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = [DOGlobalAppearance terminalBackgroundColor];
    self.navigationController.navigationBarHidden = YES;
    [self buildTerminalLogPage];
}

- (void)buildTerminalLogPage
{
    [self.terminalView removeFromSuperview];
    self.terminalView = [[DOUTerminalView alloc] initWithFrame:CGRectZero];
    self.terminalView.compactCommandRows = YES;
    self.terminalView.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:self.terminalView];
    CGFloat inset = [DOGlobalAppearance isSmallDevice] ? 10.0 : 18.0;
    [NSLayoutConstraint activateConstraints:@[
        [self.terminalView.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:8.0],
        [self.terminalView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:inset],
        [self.terminalView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-inset],
        [self.terminalView.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor constant:-8.0],
    ]];

    NSArray *records = [DOUIManager sharedInstance].logRecord ?: @[];
    NSArray *reverseLog = [[records reverseObjectEnumerator] allObjects];
    NSString *output = reverseLog.count > 0 ? [reverseLog componentsJoinedByString:@"\n"] : @"no log records";
    [self.terminalView resetWithCommand:@"dopamine --log" subtitle:[NSString stringWithFormat:@"status   failed\nreason   %@", self.errorTitle ?: @"unknown"]];
    [self.terminalView appendLine:output color:[DOGlobalAppearance terminalMutedColor]];
    [self.terminalView appendBlankLine];
    [self.terminalView appendCommand:@"share-log" detail:@"share the captured diagnostic record" enabled:YES handler:^{
        [[DOUIManager sharedInstance] shareLogRecordFromView:self.terminalView];
    }];
    __weak typeof(self) weakSelf = self;
    [self.terminalView appendBackCommandWithHandler:^{
        DOLogCrashViewController *strongSelf = weakSelf;
        if (!strongSelf) return;
        if (strongSelf.returnHandler) strongSelf.returnHandler();
        else [strongSelf.navigationController popViewControllerAnimated:NO];
    }];
}

- (void)viewWillDisappear:(BOOL)animated
{
    [super viewWillDisappear:animated];
}

@end
