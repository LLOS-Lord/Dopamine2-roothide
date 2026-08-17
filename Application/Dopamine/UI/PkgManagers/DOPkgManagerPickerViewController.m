//
//  DOPkgManagerPickerViewController.m
//  Dopamine
//
//  Created by tomt000 on 11/02/2024.
//

#import "DOPkgManagerPickerViewController.h"
#import "DOPkgManagerPickerView.h"
#import "DOEnvironmentManager.h"
#import "DOGlobalAppearance.h"


@interface DOPkgManagerPickerViewController ()
@property (nonatomic, copy) void (^completion)(void);
@end

@implementation DOPkgManagerPickerViewController

- (instancetype)initWithCompletion:(void (^)(void))completion
{
    self = [super initWithNibName:nil bundle:nil];
    if (self) {
        _completion = [completion copy];
    }
    return self;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [DOGlobalAppearance terminalBackgroundColor];
    DOPkgManagerPickerView *picker = [[DOPkgManagerPickerView alloc] initWithCallback:^(BOOL success) {
        void (^completion)(void) = self.completion;
        if (completion) {
            // First-run jailbreak flow: the selected keys are persisted by the
            // picker and the caller starts jailbreak after this page closes.
            dispatch_async(dispatch_get_main_queue(), ^{
                [self.navigationController popViewControllerAnimated:NO];
                completion();
            });
        }
        else {
            // Settings flow: preserve the original reinstall behavior.
            dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
                [[DOEnvironmentManager sharedManager] reinstallPackageManagers];
                dispatch_async(dispatch_get_main_queue(), ^{
                    [self.navigationController popViewControllerAnimated:NO];
                });
            });
        }
    }];
    picker.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:picker];
    [NSLayoutConstraint activateConstraints:@[
        [picker.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [picker.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [picker.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [picker.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor]
    ]];
}


@end
