//
//  DOMainViewController.m
//  Dopamine
//
//  Created by tomt000 on 08/01/2024.
//

#import "DOMainViewController.h"
#import "DOUIManager.h"
#import "DOEnvironmentManager.h"
#import "DOJailbreaker.h"
#import "DOGlobalAppearance.h"
#import "DOActionMenuButton.h"
#import "DOUpdateViewController.h"
#import "DOLogCrashViewController.h"
#import "DOPkgManagerPickerViewController.h"
#import <pthread.h>
#import <sys/utsname.h>
#import <libjailbreak/libjailbreak.h>

@interface DOMainViewController ()

@property DOJailbreakButton *jailbreakBtn;
@property NSArray<NSLayoutConstraint *> *jailbreakButtonConstraints;
@property DOActionMenuButton *updateButton;
@property (nonatomic, strong) DOUTerminalView *terminalView;
@property(nonatomic) BOOL hideStatusBar;
@property(nonatomic) BOOL hideHomeIndicator;

- (void)rebuildTerminalHome;

@end

@implementation DOMainViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [self setupTerminalBackground];
    [self setupStack];
}

- (void)setupTerminalBackground
{
    self.view.backgroundColor = [DOGlobalAppearance terminalBackgroundColor];
}

-(void)setupStack
{
    self.terminalView = [[DOUTerminalView alloc] initWithFrame:CGRectZero];
    self.terminalView.compactCommandRows = YES;
    self.terminalView.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:self.terminalView];
    self.jailbreakBtn = [[DOJailbreakButton alloc] initWithAction:[UIAction actionWithTitle:@"" image:nil identifier:@"terminal-jailbreak" handler:^(__kindof UIAction *action) {}]];

    CGFloat horizontalInset = [DOGlobalAppearance isSmallDevice] ? 12.0 : 18.0;
    [NSLayoutConstraint activateConstraints:@[
        [self.terminalView.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:10.0],
        [self.terminalView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:horizontalInset],
        [self.terminalView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-horizontalInset],
        [self.terminalView.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor constant:-10.0],
    ]];

    [self rebuildTerminalHome];
}

- (void)rebuildTerminalHome
{
    DOEnvironmentManager *environmentManager = [DOEnvironmentManager sharedManager];
    BOOL isSupported = environmentManager.isSupported;
    BOOL isJailbroken = environmentManager.isJailbroken;
    struct utsname systemUname;
    uname(&systemUname);
    NSString *deviceIdentifier = [NSString stringWithUTF8String:systemUname.machine] ?: @"unknown";
    NSString *kernelVersion = [NSString stringWithUTF8String:systemUname.release] ?: @"unknown";
    NSString *support = environmentManager.versionSupportString ?: @"unknown";
    NSString *build = environmentManager.appVersionDisplayString ?: @"unknown";
    NSTimeInterval systemUptime = NSProcessInfo.processInfo.systemUptime;
    NSInteger uptimeMinutes = MAX(0, (NSInteger)(systemUptime / 60.0));
    NSString *uptime = [NSString stringWithFormat:@"%ldh %02ldm", (long)(uptimeMinutes / 60), (long)(uptimeMinutes % 60)];
    NSString *subtitle = [NSString stringWithFormat:@"os       %@\nhost     %@\nkernel   %@\nbuild    %@\nuptime   %@\nsupport  %@", UIDevice.currentDevice.systemVersion ?: @"unknown", deviceIdentifier, kernelVersion, build, uptime, support];

    [self.terminalView resetWithCommand:@"" subtitle:subtitle];
    [self.terminalView appendLine:@"Tap an option below to start…" color:[DOGlobalAppearance terminalMutedColor]];
    [self.terminalView appendBlankLine];
    [self.terminalView appendBlankLine];

    NSString *jailbreakDetail = isSupported ? (isJailbroken ? @"environment is active" : @"run the selected exploit path") : @"this device or firmware is outside the supported window";
    NSString *jailbreakTitle = isJailbroken ? @"jailbreak active" : @"jailbreak do";

    [self.terminalView appendCommand:jailbreakTitle detail:jailbreakDetail enabled:(!isJailbroken && isSupported) handler:^{
        [self beginTerminalJailbreak];
    }];
    [self.terminalView appendCommand:@"settings" detail:@"configure exploit, runtime and appearance" enabled:YES handler:^{
        [self showTerminalSettings];
    }];
    [self.terminalView appendCommand:@"credits" detail:@"project, contributors and licenses" enabled:YES handler:^{
        [self showTerminalCredits];
    }];
    [self.terminalView appendBlankLine];

    [self.terminalView appendCommand:@"respring" detail:@"restart SpringBoard (requires jailbreak)" enabled:isJailbroken handler:^{
        [self fadeToBlack:^{ [[DOEnvironmentManager sharedManager] respring]; }];
    }];
    [self.terminalView appendCommand:@"reboot-userspace" detail:@"restart the userspace environment" enabled:isJailbroken handler:^{
        [self fadeToBlack:^{ [[DOEnvironmentManager sharedManager] rebootUserspace]; }];
    }];
    [self.terminalView appendCommand:@"reboot-device" detail:@"restart the device" enabled:isJailbroken handler:^{
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:DOLocalizedString(@"Menu_Reboot_Device_Title") message:DOLocalizedString(@"Alert_Reboot_Device_Message") preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:DOLocalizedString(@"Button_Cancel") style:UIAlertActionStyleCancel handler:nil]];
        [alert addAction:[UIAlertAction actionWithTitle:DOLocalizedString(@"Button_Reboot") style:UIAlertActionStyleDestructive handler:^(UIAlertAction *action) {
            [self fadeToBlack:^{ [[DOEnvironmentManager sharedManager] reboot]; }];
        }]];
        [self presentViewController:alert animated:YES completion:nil];
    }];

}

- (void)beginTerminalJailbreak
{
    if (otherJailbreakActived(false)) {
        UIAlertController *alertController = [UIAlertController alertControllerWithTitle:DOLocalizedString(@"Error") message:DOLocalizedString(@"Your device currently has another jailbreak activated, please reboot device.") preferredStyle:UIAlertControllerStyleAlert];
        [alertController addAction:[UIAlertAction actionWithTitle:DOLocalizedString(@"Button_Close") style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:alertController animated:YES completion:nil];
        return;
    }

    // Dopamine's first-run flow requires an explicit package-manager choice.
    // The terminal home bypasses the legacy expanded button, so present the
    // same picker here before starting the jailbreak pipeline.
    if ([DOUIManager sharedInstance].enabledPackageManagerKeys.count == 0) {
        __weak typeof(self) weakSelf = self;
        DOPkgManagerPickerViewController *picker = [[DOPkgManagerPickerViewController alloc] initWithCompletion:^{
            __strong typeof(weakSelf) strongSelf = weakSelf;
            if (!strongSelf) return;
            [strongSelf beginTerminalJailbreak];
        }];
        [self.navigationController pushViewController:picker animated:NO];
        return;
    }

    [self.terminalView beginProgress];
    [[DOUIManager sharedInstance] setLogView:self.terminalView];
    [self startJailbreak];
}

- (void)pushTerminalController:(UIViewController *)controller command:(NSString *)command
{
    (void)command;
    if (!controller) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self.navigationController.topViewController != self) return;
        [self.navigationController pushViewController:controller animated:NO];
    });
}

- (void)showTerminalSettings
{
    DOSettingsController *controller = [[DOSettingsController alloc] init];
    [self pushTerminalController:controller command:@"dopamine --settings"];
}

- (void)showTerminalCredits
{
    DOCreditsViewController *controller = [[DOCreditsViewController alloc] init];
    [self pushTerminalController:controller command:@"dopamine --credits"];
}

- (NSString *)jailbreakButtonTitle
{
    BOOL isJailbroken = [[DOEnvironmentManager sharedManager] isJailbroken];
    BOOL isSupported = [[DOEnvironmentManager sharedManager] isSupported];
    BOOL removeJailbreakEnabled = [[DOPreferenceManager sharedManager] boolPreferenceValueForKey:@"removeJailbreakEnabled" fallback:NO];

    NSString *jailbreakButtonTitle = DOLocalizedString(@"Button_Jailbreak_Title");
    if (!isSupported)
        jailbreakButtonTitle = DOLocalizedString(@"Unsupported");
    else if (isJailbroken)
        jailbreakButtonTitle = DOLocalizedString(@"Status_Title_Jailbroken");
    else if (removeJailbreakEnabled)
        jailbreakButtonTitle = DOLocalizedString(@"Button_Remove_Jailbreak");
    
    return jailbreakButtonTitle;
}

- (void)viewWillAppear:(BOOL)animated
{
    [super viewWillAppear:animated];
    [self.jailbreakBtn.button setTitle:[self jailbreakButtonTitle] forState:UIControlStateNormal];
}

- (void)startJailbreak
{
    DOJailbreaker *jailbreaker = [[DOJailbreaker alloc] init];

    [[DOUIManager sharedInstance] startLogCapture];
    
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^{

        //We need to get the preconfig mutex to start the jailbreak (self.jailbreakBtn.canStartJailbreak)
        [self.jailbreakBtn lockMutex];
        dispatch_async(dispatch_get_main_queue(), ^{
            self.hideHomeIndicator = YES;
        });

        NSError *error;
        BOOL didRemove = NO;
        BOOL showLogs = YES;
        [jailbreaker runWithError:&error didRemoveJailbreak:&didRemove showLogs:&showLogs];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (error && showLogs) {
                [[DOUIManager sharedInstance] sendLog:[NSString stringWithFormat:@"Jailbreak failed with error: %@", error] debug:NO];
                __weak typeof(self) weakSelf = self;
                DOLogCrashViewController *logController = [[DOLogCrashViewController alloc] initWithTitle:[error localizedDescription] returnHandler:^{
                    DOMainViewController *strongSelf = weakSelf;
                    if (!strongSelf) return;
                    [[DOUIManager sharedInstance] setLogView:strongSelf.terminalView];
                    [strongSelf.navigationController popToRootViewControllerAnimated:NO];
                    [strongSelf rebuildTerminalHome];
                    strongSelf.hideStatusBar = NO;
                    strongSelf.hideHomeIndicator = NO;
                }];
                [self.navigationController pushViewController:logController animated:NO];
            }
            else if (error && !showLogs) {
                // Used when there is an error that is explainable in such detail that additional logs are not needed
                UIAlertController *alertController = [UIAlertController alertControllerWithTitle:DOLocalizedString(@"Log_Error") message:[error localizedDescription] preferredStyle:UIAlertControllerStyleAlert];
                UIAlertAction *rebootAction = [UIAlertAction actionWithTitle:DOLocalizedString(@"Button_Reboot") style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
                    exec_cmd_trusted(JBROOT_PATH("/sbin/reboot"), NULL);
                }];
                [alertController addAction:rebootAction];
                [self presentViewController:alertController animated:YES completion:nil];
            }
            else if (didRemove) {
                UIAlertController *alertController = [UIAlertController alertControllerWithTitle:DOLocalizedString(@"Removed_Jailbreak_Alert_Title") message:DOLocalizedString(@"Removed_Jailbreak_Alert_Message") preferredStyle:UIAlertControllerStyleAlert];
                UIAlertAction *rebootAction = [UIAlertAction actionWithTitle:DOLocalizedString(@"Button_Close") style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
                    exit(0);
                }];
                [alertController addAction:rebootAction];
                [self presentViewController:alertController animated:YES completion:nil];
            }
            else {
                // No errors
                [[DOUIManager sharedInstance] completeJailbreak];
                [self fadeToBlack: ^{
                    [jailbreaker finalize];
                }];
            }
        });
        [self.jailbreakBtn unlockMutex];
    });
}

-(void)setupUpdateAvailable:(BOOL)environmentUpdate
{
    if (self.jailbreakBtn.didExpand) return;

    NSString *releaseFrom = [[DOUIManager sharedInstance] getLaunchedReleaseTag];
    NSString *releaseTo = [[DOUIManager sharedInstance] getLatestReleaseTag];
    if (environmentUpdate) {
        releaseFrom = [[DOEnvironmentManager sharedManager] jailbrokenVersion];
        releaseTo = [[DOUIManager sharedInstance] getLaunchedReleaseTag];
    }

    [self.terminalView appendBlankLine];
    [self.terminalView appendCommand:(environmentUpdate ? @"update --environment" : @"update --available") detail:nil enabled:YES handler:^{
        [self.navigationController pushViewController:[[DOUpdateViewController alloc] initFromTag:releaseFrom toTag:releaseTo] animated:NO];
    }];
}

-(void)simulateJailbreak
{
    // Let's simulate a "jailbreak" using grand central dispatch

    DOUIManager *uiManager = [DOUIManager sharedInstance];

    static BOOL didFinish = NO; //not thread safe lol
    

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        [uiManager completeJailbreak];
        [uiManager sendLog:@"Rebooting Userspace" debug: NO];
        didFinish = YES;
        [self fadeToBlack: ^{

        }];
    });

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        [NSThread sleepForTimeInterval:0.2];
        [uiManager sendLog:@"Launching kexploitd" debug: NO];
        [NSThread sleepForTimeInterval:0.5];
        [uiManager sendLog:@"Launching oobPCI" debug: NO];
        [NSThread sleepForTimeInterval:0.15];
        [uiManager sendLog:@"Gaining r/w" debug: NO];
        [NSThread sleepForTimeInterval:0.8];
        [uiManager sendLog:@"Patchfinding" debug: NO];
        NSArray *types = @[@"AMFI", @"PAC", @"KTRR", @"KPP", @"PPL", @"KPF", @"APRR", @"AMCC", @"PAN", @"PXN", @"ASLR", @"OPA"]; //Ever heard of the legendary opa bypass
        while (true)
        {
            [NSThread sleepForTimeInterval:0.6 * rand() / RAND_MAX];
            if (didFinish) break;
            NSString *type = types[arc4random_uniform((uint32_t)types.count)];
            [uiManager sendLog:[NSString stringWithFormat:@"Bypassing %@", type] debug: NO];
        }
    });
}

- (void)fadeToBlack:(void (^)(void))completion
{
    // Terminal actions execute without the legacy shrinking/fade animation.
    self.hideStatusBar = YES;
    if (completion) completion();
}

#pragma mark - Action Menu Delegate

- (BOOL)actionMenuShowsChevronForAction:(UIAction *)action
{
    if ([action.identifier isEqualToString:@"settings"] || [action.identifier isEqualToString:@"credits"]) return YES;
    return NO;
}

- (BOOL)actionMenuActionIsEnabled:(UIAction *)action
{
    if ([action.identifier isEqualToString:@"respring"] || [action.identifier isEqualToString:@"reboot-userspace"]) {
        return [[DOEnvironmentManager sharedManager] isJailbroken];
    }
    if ([action.identifier isEqualToString:@"reboot-device"]) {
        return [[DOEnvironmentManager sharedManager] isJailbroken];
    }
    return YES;
}

#pragma mark - Status Bar

- (UIStatusBarStyle)preferredStatusBarStyle
{
    return UIStatusBarStyleDarkContent;
}

- (BOOL)prefersStatusBarHidden
{
    return self.hideStatusBar;
}

- (BOOL)prefersHomeIndicatorAutoHidden
{
    return self.hideHomeIndicator;
}

- (void)setHideStatusBar:(BOOL)hideStatusBar
{
    _hideStatusBar = hideStatusBar;
    [self setNeedsStatusBarAppearanceUpdate];
}

- (void)setHideHomeIndicator:(BOOL)hideHomeIndicator
{
    _hideHomeIndicator = hideHomeIndicator;
    [self setNeedsUpdateOfHomeIndicatorAutoHidden];
}

@end
