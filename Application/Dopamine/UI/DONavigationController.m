//
//  DONavigationController.m
//  Dopamine
//
//  Created by tomt000 on 04/01/2024.
//

#import "DONavigationController.h"
#import <objc/runtime.h>
#import "DOModalBackAction.h"
#import "DOGlobalAppearance.h"
#import "DOThemeManager.h"

@interface DONavigationController ()

@property (nonatomic) UIImageView *backgroundImageView;
@property (nonatomic) DOMainViewController *mainView;
@property (nonatomic) DOModalBackAction *backAction;

@end

@interface UINavigationController (Private)
-(CGRect)_frameForViewController:(id)arg1;
@end

@implementation DONavigationController

- (void)viewDidLoad
{
    [self setupBackground];
    [super viewDidLoad];
    [self setNavigationBarHidden:YES];
    [self pushViewController:(self.mainView = [[DOMainViewController alloc] init]) animated:NO];
    [self setDelegate:self];
    [self setOverrideUserInterfaceStyle:UIUserInterfaceStyleLight];
}

- (void)setupBackground
{
    // The navigation container is a terminal canvas, not a themed modal host.
    self.view.backgroundColor = [DOGlobalAppearance terminalBackgroundColor];
    self.backAction = [[DOModalBackAction alloc] initWithAction:^{
        [self popViewControllerAnimated:NO];
    }];
    self.backAction.translatesAutoresizingMaskIntoConstraints = NO;
    self.backAction.hidden = YES;
    [self.view addSubview:self.backAction];
    [NSLayoutConstraint activateConstraints:@[
        [self.backAction.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [self.backAction.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [self.backAction.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [self.backAction.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
    ]];
}

- (void)setBackgroundDimmed:(BOOL)dimmed
{
    self.view.backgroundColor = [DOGlobalAppearance terminalBackgroundColor];
    self.backAction.hidden = YES;
}

#pragma mark - Delegate

- (id<UIViewControllerAnimatedTransitioning>)navigationController:(UINavigationController *)navigationController
           animationControllerForOperation:(UINavigationControllerOperation)operation
                        fromViewController:(UIViewController *)fromVC
                          toViewController:(UIViewController *)toVC {
    // Terminal pages switch sessions without UIKit slide/scale animation.
    return nil;
}


- (void)navigationController:(UINavigationController *)navigationController willShowViewController:(UIViewController *)viewController animated:(BOOL)animated
{
    [self setBackgroundDimmed:![viewController isKindOfClass:[DOMainViewController class]]];
    [self.backAction setIgnoreFrame:[self _frameForViewController:viewController]];
}

#pragma mark - Status Bar

- (UIStatusBarStyle)preferredStatusBarStyle
{
    return UIStatusBarStyleDarkContent;
}

#pragma mark - Overrides

-(CGRect)_frameForViewController:(id)viewController
{
    // Terminal pages are full-screen. The old centered modal frame created the
    // rectangular black island visible behind Settings and Credits.
    return [super _frameForViewController:viewController];
}


@end
