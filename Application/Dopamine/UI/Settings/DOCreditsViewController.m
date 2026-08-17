//
//  DOCreditsViewController.m
//  Dopamine
//

#import "DOCreditsViewController.h"
#import "DOLicenseViewController.h"
#import "DOUIManager.h"
#import "DOEnvironmentManager.h"
#import "DOGlobalAppearance.h"

@interface DOCreditsViewController ()
@property (nonatomic, strong) DOUTerminalView *terminalView;
@end

@implementation DOCreditsViewController

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.title = DOLocalizedString(@"Menu_Credits_Title");
    [self buildTerminalCreditsUI];
}

- (void)buildTerminalCreditsUI
{
    self.view.backgroundColor = [DOGlobalAppearance terminalBackgroundColor];
    self.navigationController.navigationBarHidden = YES;
    [self.terminalView removeFromSuperview];
    self.terminalView = [[DOUTerminalView alloc] initWithFrame:CGRectZero];
    self.terminalView.compactCommandRows = YES;
    self.terminalView.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:self.terminalView];
    CGFloat inset = [DOGlobalAppearance isSmallDevice] ? 12.0 : 18.0;
    [NSLayoutConstraint activateConstraints:@[
        [self.terminalView.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:10.0],
        [self.terminalView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:inset],
        [self.terminalView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-inset],
        [self.terminalView.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor constant:-10.0],
    ]];

    NSString *version = [DOEnvironmentManager sharedManager].appVersionDisplayString ?: @"unknown";
    [self.terminalView resetWithCommand:@"" subtitle:[NSString stringWithFormat:@"version  %@\nsource   LLOS-Lord/Dopamine2-roothide", version]];
    [self.terminalView appendBlankLine];
    [self.terminalView appendCommand:@"source" detail:@"open the GitHub repository" enabled:YES handler:^{ [self openSourceCode]; }];
    [self.terminalView appendCommand:@"discord" detail:@"join the community" enabled:YES handler:^{ [self openDiscord]; }];
    [self.terminalView appendCommand:@"license" detail:@"read bundled licenses" enabled:YES handler:^{ [self openLicense]; }];

    NSString *creditsPath = [[NSBundle mainBundle] pathForResource:@"Credits" ofType:@"plist"];
    NSDictionary *credits = creditsPath ? [NSDictionary dictionaryWithContentsOfFile:creditsPath] : nil;
    NSArray *items = credits[@"items"];
    for (NSDictionary *item in items) {
        NSArray *names = item[@"names"];
        NSString *label = item[@"label"];
        if (names.count == 0 || label.length == 0) continue;
        [self.terminalView appendBlankLine];
        for (NSDictionary *entry in names) {
            NSString *name = entry[@"name"] ?: @"unknown";
            NSString *link = entry[@"link"];
            [self.terminalView appendCommand:name detail:link ?: @"" enabled:(link.length > 0) handler:^{
                NSURL *url = [NSURL URLWithString:link];
                if (url) [[UIApplication sharedApplication] openURL:url options:@{} completionHandler:nil];
            }];
        }
    }
    [self.terminalView appendBackCommandWithHandler:^{ [self.navigationController popViewControllerAnimated:NO]; }];
}

- (void)openSourceCode
{
    [[UIApplication sharedApplication] openURL:[NSURL URLWithString:@"https://github.com/LLOS-Lord/Dopamine2-roothide"] options:@{} completionHandler:nil];
}

- (void)openDiscord
{
    [[UIApplication sharedApplication] openURL:[NSURL URLWithString:@"https://discord.gg/jb"] options:@{} completionHandler:nil];
}

- (void)openLicense
{
    [self.navigationController pushViewController:[[DOLicenseViewController alloc] init] animated:NO];
}

@end
