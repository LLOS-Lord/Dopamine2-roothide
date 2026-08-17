//
//  DOSettingsController.m
//  Dopamine
//
//  Created by tomt000 on 08/01/2024.
//

#import "DOSettingsController.h"
#import <objc/runtime.h>
#import <Photos/Photos.h>
#import <libjailbreak/util.h>
#import "DOUIManager.h"
#import "DOPkgManagerPickerViewController.h"
#import "DOEnvironmentManager.h"
#import "DOExploitManager.h"
#import "DOThemeManager.h"
#import "DOGlobalAppearance.h"
#import "DOSceneDelegate.h"

@interface DOSettingsRowView : UIControl
@property (nonatomic, copy) void (^tapHandler)(void);
- (instancetype)initWithIcon:(NSString *)icon title:(NSString *)title detail:(NSString *)detail accessory:(UIView *)accessory;
@end

@implementation DOSettingsRowView

- (instancetype)initWithIcon:(NSString *)icon title:(NSString *)title detail:(NSString *)detail accessory:(UIView *)accessory
{
    self = [super initWithFrame:CGRectZero];
    if (!self) return nil;
    self.translatesAutoresizingMaskIntoConstraints = NO;
    self.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.045];
    self.layer.cornerRadius = 16.0;
    self.layer.cornerCurve = kCACornerCurveContinuous;
    self.layer.borderWidth = 1.0;
    self.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.07].CGColor;
    self.accessibilityTraits = UIAccessibilityTraitButton;

    UIImageView *iconView = [[UIImageView alloc] initWithImage:[UIImage systemImageNamed:icon]];
    iconView.translatesAutoresizingMaskIntoConstraints = NO;
    iconView.tintColor = [UIColor colorWithRed:0.30 green:0.90 blue:0.54 alpha:0.95];
    iconView.contentMode = UIViewContentModeScaleAspectFit;
    [self addSubview:iconView];

    UIStackView *textStack = [[UIStackView alloc] init];
    textStack.translatesAutoresizingMaskIntoConstraints = NO;
    textStack.axis = UILayoutConstraintAxisVertical;
    textStack.spacing = 2.0;
    [self addSubview:textStack];

    UILabel *titleLabel = [[UILabel alloc] init];
    titleLabel.text = title;
    titleLabel.textColor = UIColor.whiteColor;
    titleLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    titleLabel.adjustsFontForContentSizeCategory = YES;
    [textStack addArrangedSubview:titleLabel];

    if (detail.length > 0) {
        UILabel *detailLabel = [[UILabel alloc] init];
        detailLabel.text = detail;
        detailLabel.textColor = [UIColor colorWithWhite:1.0 alpha:0.52];
        detailLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
        detailLabel.adjustsFontForContentSizeCategory = YES;
        detailLabel.numberOfLines = 2;
        [textStack addArrangedSubview:detailLabel];
    }

    accessory.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:accessory];

    [NSLayoutConstraint activateConstraints:@[
        [self.heightAnchor constraintGreaterThanOrEqualToConstant:64.0],
        [iconView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:16.0],
        [iconView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [iconView.widthAnchor constraintEqualToConstant:23.0],
        [iconView.heightAnchor constraintEqualToConstant:23.0],
        [textStack.leadingAnchor constraintEqualToAnchor:iconView.trailingAnchor constant:13.0],
        [textStack.topAnchor constraintGreaterThanOrEqualToAnchor:self.topAnchor constant:11.0],
        [textStack.bottomAnchor constraintLessThanOrEqualToAnchor:self.bottomAnchor constant:-11.0],
        [textStack.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [textStack.trailingAnchor constraintLessThanOrEqualToAnchor:accessory.leadingAnchor constant:-10.0],
        [accessory.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-16.0],
        [accessory.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
    ]];

    [self addTarget:self action:@selector(rowPressed) forControlEvents:UIControlEventTouchUpInside];
    return self;
}

- (void)rowPressed
{
    if (self.tapHandler) self.tapHandler();
}

@end

@interface DOSettingsController ()
@property (nonatomic, strong) UIScrollView *customScrollView;
@property (nonatomic, strong) UIStackView *customStackView;
@property (nonatomic, strong) DOUTerminalView *terminalView;
@end

@implementation DOSettingsController

- (void)viewDidLoad
{
    _lastKnownTheme = [[DOThemeManager sharedInstance] enabledTheme].key;
    [super viewDidLoad];
    self.title = DOLocalizedString(@"Menu_Settings_Title");
    [self buildTerminalSettingsUI];
}

- (void)buildTerminalSettingsUI
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

    DOEnvironmentManager *env = [DOEnvironmentManager sharedManager];
    DOExploitManager *exploitManager = [DOExploitManager sharedManager];
    NSSortDescriptor *priority = [NSSortDescriptor sortDescriptorWithKey:@"priority" ascending:NO];
    _availableKernelExploits = [[exploitManager availableExploitsForType:EXPLOIT_TYPE_KERNEL] sortedArrayUsingDescriptors:@[priority]];
    _availablePACBypasses = [[exploitManager availableExploitsForType:EXPLOIT_TYPE_PAC] sortedArrayUsingDescriptors:@[priority]];
    _availablePPLBypasses = [[exploitManager availableExploitsForType:EXPLOIT_TYPE_PPL] sortedArrayUsingDescriptors:@[priority]];

    NSString *status = env.isJailbroken ? @"jailbroken" : (env.isSupported ? @"ready" : @"unsupported");
    NSString *subtitle = [NSString stringWithFormat:@"status   %@\nsupport  %@\nmode     interactive", status, env.versionSupportString ?: @"unknown"];
    [self.terminalView resetWithCommand:@"" subtitle:subtitle];
    [self.terminalView appendBlankLine];

    if (!env.isJailbroken) {
        DOExploit *kernel = exploitManager.selectedKernelExploit ?: exploitManager.preferredKernelExploit ?: _availableKernelExploits.firstObject;
        [self.terminalView appendCommand:[NSString stringWithFormat:@"kernel  %@", kernel.displayName ?: kernel.name ?: @"none"] detail:@"select kernel exploit" enabled:env.isSupported handler:^{
            [self showExploitChoices:_availableKernelExploits title:@"kernel" preferenceKey:@"selectedKernelExploit"];
        }];
        if (env.isArm64e || _availablePACBypasses.count > 0) {
            DOExploit *pac = exploitManager.selectedPACBypass ?: exploitManager.preferredPACBypass;
            [self.terminalView appendCommand:[NSString stringWithFormat:@"pac     %@", pac.displayName ?: pac.name ?: @"none"] detail:@"select PAC bypass" enabled:env.isSupported handler:^{
                [self showExploitChoices:_availablePACBypasses title:@"pac" preferenceKey:@"selectedPACBypass"];
            }];
        }
        if (env.isPPLBypassRequired || _availablePPLBypasses.count > 0 || NSProcessInfo.processInfo.operatingSystemVersion.majorVersion >= 16) {
            DOExploit *ppl = exploitManager.selectedPPLBypass ?: exploitManager.preferredPPLBypass;
            [self.terminalView appendCommand:[NSString stringWithFormat:@"ppl     %@", ppl.displayName ?: ppl.name ?: @"none"] detail:@"select PPL bypass" enabled:env.isSupported handler:^{
                [self showExploitChoices:_availablePPLBypasses title:@"ppl" preferenceKey:@"selectedPPLBypass"];
            }];
        }
        [self.terminalView appendBlankLine];
    }

    BOOL tweakInjection = env.isJailbroken ? env.isTweakInjectionEnabled : [[DOPreferenceManager sharedManager] boolPreferenceValueForKey:@"tweakInjectionEnabled" fallback:YES];
    [self.terminalView appendCommand:[NSString stringWithFormat:@"tweaks  %@", tweakInjection ? @"on" : @"off"] detail:@"toggle tweak injection" enabled:YES handler:^{ [self setTweakInjectionValue:!tweakInjection]; }];
    if (!env.isJailbroken) {
        BOOL verbose = [[DOPreferenceManager sharedManager] boolPreferenceValueForKey:@"verboseLogsEnabled" fallback:NO];
        [self.terminalView appendCommand:[NSString stringWithFormat:@"verbose  %@", verbose ? @"on" : @"off"] detail:@"toggle detailed jailbreak logs" enabled:YES handler:^{ [[DOPreferenceManager sharedManager] setPreferenceValue:@(!verbose) forKey:@"verboseLogsEnabled"]; [self buildTerminalSettingsUI]; }];
    }
    BOOL idownload = env.isJailbroken ? env.isIDownloadEnabled : [[DOPreferenceManager sharedManager] boolPreferenceValueForKey:@"idownloadEnabled" fallback:NO];
    [self.terminalView appendCommand:[NSString stringWithFormat:@"download  %@", idownload ? @"on" : @"off"] detail:@"toggle package environment downloads" enabled:YES handler:^{ [[DOPreferenceManager sharedManager] setPreferenceValue:@(!idownload) forKey:@"idownloadEnabled"]; if (env.isJailbroken) [env setIDownloadLoaded:!idownload needsUnsandbox:YES]; [self buildTerminalSettingsUI]; }];
    BOOL appJIT = env.isJailbroken ? jbclient_jbsettings_get_bool("markAppsAsDebugged") : [[DOPreferenceManager sharedManager] boolPreferenceValueForKey:@"appJITEnabled" fallback:YES];
    [self.terminalView appendCommand:[NSString stringWithFormat:@"app-jit  %@", appJIT ? @"on" : @"off"] detail:@"mark apps as debugged" enabled:YES handler:^{ [[DOPreferenceManager sharedManager] setPreferenceValue:@(!appJIT) forKey:@"appJITEnabled"]; if (env.isJailbroken) jbclient_platform_jbsettings_set_bool("markAppsAsDebugged", !appJIT); [self buildTerminalSettingsUI]; }];
    BOOL dyldPatch = env.isJailbroken ? jbclient_dyld_patch_enabled() : [[DOPreferenceManager sharedManager] boolPreferenceValueForKey:@"dyldPatchEnabled" fallback:NO];
    [self.terminalView appendCommand:[NSString stringWithFormat:@"dyld-patch  %@", dyldPatch ? @"on" : @"off"] detail:@"toggle RootHide loader patch" enabled:YES handler:^{ [self setDyldPatchValue:!dyldPatch]; }];

    [self.terminalView appendBlankLine];
    NSNumber *storedJetsam = [[DOPreferenceManager sharedManager] preferenceValueForKey:@"jetsamMultiplier"];
    NSInteger jetsamValue = env.isJailbroken ? (NSInteger)ceil(jbclient_jbsettings_get_double("jetsamMultiplier") * 2.0) : (storedJetsam ? storedJetsam.integerValue : 6);
    if (jetsamValue < 2 || jetsamValue > 8) jetsamValue = 6;
    [self.terminalView appendCommand:[NSString stringWithFormat:@"jetsam  %.1gx", jetsamValue / 2.0] detail:@"select jetsam multiplier" enabled:YES handler:^{ [self showJetsamChoices]; }];
    BOOL bootlogo = [[DOPreferenceManager sharedManager] boolPreferenceValueForKey:@"bootlogoEnabled" fallback:YES];
    BOOL customBootlogo = [[DOPreferenceManager sharedManager] boolPreferenceValueForKey:@"customBootlogoEnabled" fallback:NO];
    [self.terminalView appendCommand:[NSString stringWithFormat:@"bootlogo  %@", bootlogo ? @"on" : @"off"] detail:@"toggle boot logo" enabled:YES handler:^{ [[DOPreferenceManager sharedManager] setPreferenceValue:@(!bootlogo) forKey:@"bootlogoEnabled"]; [self buildTerminalSettingsUI]; }];
    if (bootlogo) {
        [self.terminalView appendCommand:[NSString stringWithFormat:@"custom-logo  %@", customBootlogo ? @"on" : @"off"] detail:@"toggle selected image" enabled:YES handler:^{ [[DOPreferenceManager sharedManager] setPreferenceValue:@(!customBootlogo) forKey:@"customBootlogoEnabled"]; [self buildTerminalSettingsUI]; }];
        if (customBootlogo) [self.terminalView appendCommand:@"custom-logo --select" detail:@"choose image from Photos" enabled:YES handler:^{ [self selectCustomBootlogoPressed]; }];
    }

    if (env.isJailbroken || (env.isInstalledThroughTrollStore && env.isBootstrapped)) {
        [self.terminalView appendBlankLine];
        if (env.isJailbroken) {
            [self.terminalView appendCommand:@"refresh-apps" detail:@"rescan jailbreak applications" enabled:YES handler:^{ [self refreshJailbreakAppsPressed]; }];
            [self.terminalView appendCommand:@"change-password" detail:@"authenticate and change mobile password" enabled:YES handler:^{ [self changeMobilePasswordWithAuthenticationPressed]; }];
            [self.terminalView appendCommand:@"package-managers" detail:@"reinstall or choose package manager" enabled:YES handler:^{ [self reinstallPackageManagersPressed]; }];
        }
        [self.terminalView appendCommand:@"remove-jailbreak" detail:@"delete bootstrap and jailbreak data" enabled:YES handler:^{ [self removeJailbreakPressed]; }];
    }

    [self.terminalView appendBackCommandWithHandler:^{ [self.navigationController popViewControllerAnimated:NO]; }];
}

- (void)showExploitChoices:(NSArray<DOExploit *> *)exploits title:(NSString *)title preferenceKey:(NSString *)preferenceKey
{
    [self.terminalView resetWithCommand:[NSString stringWithFormat:@"dopamine --select %@", title] subtitle:@"choose one command below\nchanges apply to the next jailbreak attempt"];
    for (DOExploit *exploit in exploits) {
        NSString *name = exploit.displayName ?: exploit.name ?: exploit.identifier;
        [self.terminalView appendCommand:name detail:exploit.identifier enabled:YES handler:^{
            [[DOPreferenceManager sharedManager] setPreferenceValue:exploit.identifier forKey:preferenceKey];
            [self buildTerminalSettingsUI];
        }];
    }
    if (![preferenceKey isEqualToString:@"selectedKernelExploit"]) {
        [self.terminalView appendCommand:@"none" detail:@"do not select a bypass" enabled:YES handler:^{
            [[DOPreferenceManager sharedManager] setPreferenceValue:@"none" forKey:preferenceKey];
            [self buildTerminalSettingsUI];
        }];
    }
    [self.terminalView appendBackCommandWithHandler:^{ [self buildTerminalSettingsUI]; }];
}

- (void)showJetsamChoices
{
    NSArray *titles = @[@"1x", @"1.5x", @"2x", @"2.5x", @"3x", @"3.5x", @"4x"];
    [self.terminalView resetWithCommand:@"dopamine --select jetsam" subtitle:@"choose memory pressure multiplier"];
    for (NSUInteger index = 0; index < titles.count; index++) {
        NSNumber *value = @(index + 2);
        [self.terminalView appendCommand:titles[index] detail:@"set multiplier" enabled:YES handler:^{
            [[DOPreferenceManager sharedManager] setPreferenceValue:value forKey:@"jetsamMultiplier"];
            if ([DOEnvironmentManager sharedManager].isJailbroken) jbclient_platform_jbsettings_set_double("jetsamMultiplier", value.doubleValue / 2.0);
            [self buildTerminalSettingsUI];
        }];
    }
    [self.terminalView appendBackCommandWithHandler:^{ [self buildTerminalSettingsUI]; }];
}

- (void)setTweakInjectionValue:(BOOL)enabled
{
    [[DOPreferenceManager sharedManager] setPreferenceValue:@(enabled) forKey:@"tweakInjectionEnabled"];
    DOEnvironmentManager *env = [DOEnvironmentManager sharedManager];
    if (!env.isJailbroken) return;
    [env setTweakInjectionEnabled:enabled];
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:DOLocalizedString(@"Alert_Tweak_Injection_Toggled_Title") message:DOLocalizedString(@"Alert_Tweak_Injection_Toggled_Body") preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:DOLocalizedString(@"Alert_Tweak_Injection_Toggled_Reboot_Later") style:UIAlertActionStyleCancel handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:DOLocalizedString(@"Alert_Tweak_Injection_Toggled_Reboot_Now") style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        [env rebootUserspace];
    }]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)setDyldPatchValue:(BOOL)enabled
{
    DOEnvironmentManager *env = [DOEnvironmentManager sharedManager];
    if (!env.isJailbroken) {
        [[DOPreferenceManager sharedManager] setPreferenceValue:@(enabled) forKey:@"dyldPatchEnabled"];
        [self buildTerminalSettingsUI];
        return;
    }
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:DOLocalizedString(@"Alert_Tweak_Injection_Toggled_Title") message:DOLocalizedString(@"Alert_Tweak_Injection_Toggled_Body") preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:DOLocalizedString(@"Button_Cancel") style:UIAlertActionStyleCancel handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:DOLocalizedString(@"Menu_Reboot_Userspace_Title") style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        if (jbclient_set_dyld_patch(enabled) == 0) {
            [[DOPreferenceManager sharedManager] setPreferenceValue:@(enabled) forKey:@"dyldPatchEnabled"];
            [env rebootUserspace];
        }
    }]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)viewWillAppear:(BOOL)arg1
{
    [super viewWillAppear:arg1];
    if (self.terminalView) [self buildTerminalSettingsUI];
    if (_lastKnownTheme != [[DOThemeManager sharedInstance] enabledTheme].key)
    {
        [DOSceneDelegate relaunch];
        NSString *icon = [[DOThemeManager sharedInstance] enabledTheme].icon;
        [[UIApplication sharedApplication] setAlternateIconName:icon completionHandler:^(NSError * _Nullable error) {
            if (error)
                NSLog(@"Error changing app icon: %@", error);
        }];

        if ([DOEnvironmentManager sharedManager].isJailbroken) {
            dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
                [[DOEnvironmentManager sharedManager] updateBootLogo];
            });
        }
    }
}

- (void)selectCustomBootlogoPressed
{
    PHAuthorizationStatus status = [PHPhotoLibrary authorizationStatus];
    if (status == PHAuthorizationStatusDenied || status == PHAuthorizationStatusRestricted) {
        return;
    } else if (status == PHAuthorizationStatusNotDetermined) {
        [PHPhotoLibrary requestAuthorization:^(PHAuthorizationStatus status) {
            if (status == PHAuthorizationStatusAuthorized) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [self selectCustomBootlogoPressed];
                });
            }
        }];
        return;
    }

    UIImagePickerController *picker = [[UIImagePickerController alloc] init];
    picker.delegate = self;
    picker.sourceType = UIImagePickerControllerSourceTypePhotoLibrary;
    [self presentViewController:picker animated:YES completion:nil];
}

#pragma mark - Boot Logo Picker

- (void)imagePickerController:(UIImagePickerController *)picker didFinishPickingMediaWithInfo:(NSDictionary<NSString *,id> *)info {
    UIImage *chosenImage = info[UIImagePickerControllerEditedImage] ?: info[UIImagePickerControllerOriginalImage];
    if (![chosenImage isKindOfClass:[UIImage class]] || !chosenImage.CGImage || chosenImage.size.width <= 0.0 || chosenImage.size.height <= 0.0) {
        [picker dismissViewControllerAnimated:YES completion:nil];
        return;
    }

    // Normalize orientation before persisting the custom logo.
    UIGraphicsBeginImageContextWithOptions(chosenImage.size, NO, 1.0);
    if (!UIGraphicsGetCurrentContext()) {
        [picker dismissViewControllerAnimated:YES completion:nil];
        return;
    }
    [chosenImage drawInRect:CGRectMake(0, 0, chosenImage.size.width, chosenImage.size.height)];
    UIImage *normalizedImage = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    NSData *pngData = UIImagePNGRepresentation(normalizedImage ?: chosenImage);
    if (pngData.length > 0) {
        [pngData writeToFile:[DOUIManager sharedInstance].bootlogoPath atomically:YES];
    }

    if ([DOEnvironmentManager sharedManager].isJailbroken && pngData.length > 0) {
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            [[DOEnvironmentManager sharedManager] updateBootLogo];
        });
    }

    [picker dismissViewControllerAnimated:YES completion:nil];
}

- (void)imagePickerControllerDidCancel:(UIImagePickerController *)picker {
    [picker dismissViewControllerAnimated:YES completion:nil];
}

#pragma mark - Button Actions

- (void)refreshJailbreakAppsPressed
{
    [[DOEnvironmentManager sharedManager] refreshJailbreakApps];
}

- (void)reinstallPackageManagersPressed
{
    [self.navigationController pushViewController:[[DOPkgManagerPickerViewController alloc] init] animated:NO];
}

- (void)changeMobilePasswordWithAuthenticationPressed
{
	LAContext *context = [[LAContext alloc] init];
	NSError *authError = nil;
	NSString *reason = DOLocalizedString(@"Password_Auth_Required");
	
	if ([context canEvaluatePolicy:LAPolicyDeviceOwnerAuthentication error:&authError]) {
		[context evaluatePolicy:LAPolicyDeviceOwnerAuthentication
			localizedReason:reason
			reply:^(BOOL success, NSError * _Nullable error) {
			dispatch_async(dispatch_get_main_queue(), ^{
				if (success) {
					[self changeMobilePassword];
				}
			});
		}];
	}
	else {
		[self changeMobilePassword];
	}
}

- (void)changeMobilePassword
{
    UIAlertController *changeMobilePasswordAlert = [UIAlertController alertControllerWithTitle:DOLocalizedString(@"Button_Change_Mobile_Password") message:DOLocalizedString(@"Alert_Change_Mobile_Password_Body") preferredStyle:UIAlertControllerStyleAlert];
    
    [changeMobilePasswordAlert addTextFieldWithConfigurationHandler:^(UITextField * _Nonnull textField) {
        textField.placeholder = DOLocalizedString(@"Password_Placeholder");
        textField.secureTextEntry = YES;
    }];
    
    [changeMobilePasswordAlert addTextFieldWithConfigurationHandler:^(UITextField * _Nonnull textField) {
        textField.placeholder = DOLocalizedString(@"Repeat_Password_Placeholder");
        textField.secureTextEntry = YES;
    }];
    
    UIAlertAction *changeButton = [UIAlertAction actionWithTitle:DOLocalizedString(@"Button_Change") style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action){
        NSString *password = changeMobilePasswordAlert.textFields[0].text;
        NSString *repeatPassword = changeMobilePasswordAlert.textFields[1].text;
        if (![password isEqualToString:repeatPassword]) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self changeMobilePassword];
            });
        }
        else {
            [[DOEnvironmentManager sharedManager] changeMobilePassword:password];
        }
    }];
    UIAlertAction *cancelAction = [UIAlertAction actionWithTitle:DOLocalizedString(@"Button_Cancel") style:UIAlertActionStyleCancel handler:nil];
    [changeMobilePasswordAlert addAction:changeButton];
    [changeMobilePasswordAlert addAction:cancelAction];
    [self presentViewController:changeMobilePasswordAlert animated:YES completion:nil];
}


- (void)removeJailbreakPressed
{
    UIAlertController *confirmationAlertController = [UIAlertController alertControllerWithTitle:DOLocalizedString(@"Alert_Remove_Jailbreak_Title") message:DOLocalizedString(@"Alert_Remove_Jailbreak_Pressed_Body") preferredStyle:UIAlertControllerStyleAlert];
    UIAlertAction *uninstallAction = [UIAlertAction actionWithTitle:DOLocalizedString(@"Button_Continue") style:UIAlertActionStyleDestructive handler:^(UIAlertAction * _Nonnull action) {
        [[DOEnvironmentManager sharedManager] deleteBootstrap];
        if ([DOEnvironmentManager sharedManager].isJailbroken) {
            [[DOEnvironmentManager sharedManager] reboot];
        }
        else {
            if (gSystemInfo.jailbreakInfo.rootPath) {
                free(gSystemInfo.jailbreakInfo.rootPath);
                gSystemInfo.jailbreakInfo.rootPath = NULL;
                [[DOEnvironmentManager sharedManager] locateJailbreakRoot];
            }
            [self buildTerminalSettingsUI];
        }
    }];
    UIAlertAction *cancelAction = [UIAlertAction actionWithTitle:DOLocalizedString(@"Button_Cancel") style:UIAlertActionStyleDefault handler:nil];
    [confirmationAlertController addAction:uninstallAction];
    [confirmationAlertController addAction:cancelAction];
    [self presentViewController:confirmationAlertController animated:YES completion:nil];
}

- (void)resetSettingsPressed
{
    [[DOUIManager sharedInstance] resetSettings];
    [self.navigationController popToRootViewControllerAnimated:YES];
    [self buildTerminalSettingsUI];
}



@end
