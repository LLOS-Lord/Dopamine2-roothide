//
//  DOLicenseViewController.m
//  Dopamine
//

#import "DOLicenseViewController.h"
#import "DOGlobalAppearance.h"

@interface DOLicenseViewController ()
@property (nonatomic, strong) DOUTerminalView *terminalView;
@property (nonatomic, assign) NSInteger selectedLicense;
@end

@implementation DOLicenseViewController

+ (NSArray<NSDictionary *> *)licenses
{
    return @[
        @{ @"name": @"Dopamine", @"file": @"LICENSE" },
        @{ @"name": @"kfd", @"file": @"LICENSE_kfd" },
        @{ @"name": @"weightBufs", @"file": @"LICENSE_weightBufs" },
        @{ @"name": @"libgrabkernel2", @"file": @"LICENSE_libgrabkernel2" },
        @{ @"name": @"ElleKit", @"file": @"LICENSE_ElleKit" },
        @{ @"name": @"Fugu15", @"file": @"LICENSE_Fugu15" },
        @{ @"name": @"Fugu15_Rootful", @"file": @"LICENSE_Fugu15_Rootful" },
        @{ @"name": @"libc", @"file": @"LICENSE_libc" },
        @{ @"name": @"ChOma", @"file": @"LICENSE_ChOma" },
        @{ @"name": @"XPF", @"file": @"LICENSE_XPF" },
        @{ @"name": @"opainject", @"file": @"LICENSE_opainject" },
        @{ @"name": @"plooshinit", @"file": @"LICENSE_plooshinit" },
        @{ @"name": @"dimentio", @"file": @"LICENSE_dimentio" },
        @{ @"name": @"Procursus", @"file": @"LICENSE_Procursus" },
        @{ @"name": @"Sileo", @"file": @"LICENSE_Sileo" },
        @{ @"name": @"Zebra", @"file": @"LICENSE_Zebra" },
    ];
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = [DOGlobalAppearance terminalBackgroundColor];
    self.navigationController.navigationBarHidden = YES;
    self.selectedLicense = 0;
    [self buildTerminalLicensePage];
}

- (void)buildTerminalLicensePage
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

    NSArray<NSDictionary *> *licenses = [DOLicenseViewController licenses];
    NSDictionary *selected = licenses[self.selectedLicense];
    NSString *file = selected[@"file"];
    NSString *content = [NSString stringWithContentsOfFile:[[NSBundle mainBundle] pathForResource:file ofType:@"md"] encoding:NSUTF8StringEncoding error:nil] ?: @"license unavailable";
    [self.terminalView resetWithCommand:@"dopamine --license" subtitle:[NSString stringWithFormat:@"selected  %@\nfiles    %lu", selected[@"name"], (unsigned long)licenses.count]];
    for (NSUInteger index = 0; index < licenses.count; index++) {
        NSDictionary *license = licenses[index];
        BOOL selectedState = index == self.selectedLicense;
        [self.terminalView appendCommand:[NSString stringWithFormat:@"%@%@", selectedState ? @"* " : @"  ", license[@"name"]] detail:@"open document" enabled:YES handler:^{
            self.selectedLicense = index;
            [self buildTerminalLicensePage];
        }];
    }
    [self.terminalView appendBlankLine];
    [self.terminalView appendLine:content color:[DOGlobalAppearance terminalTextColor]];
    [self.terminalView appendBackCommandWithHandler:^{ [self.navigationController popViewControllerAnimated:NO]; }];
}

@end
