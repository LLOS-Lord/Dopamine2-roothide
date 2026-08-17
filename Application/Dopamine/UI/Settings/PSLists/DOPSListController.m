//
//  DOPSListController.m
//  Dopamine
//

#import "DOPSListController.h"
#import "DOThemeManager.h"

@implementation DOPSListController

- (void)viewDidLoad
{
    [super viewDidLoad];

    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    self.view.backgroundColor = [UIColor colorWithRed:0.025 green:0.035 blue:0.055 alpha:1.0];
    self.view.layer.cornerRadius = 0;
    self.view.layer.masksToBounds = NO;

    _table.separatorStyle = UITableViewCellSeparatorStyleNone;
    _table.backgroundColor = UIColor.clearColor;
    _table.contentInset = UIEdgeInsetsMake(8.0, 0.0, 24.0, 0.0);
    _table.scrollIndicatorInsets = UIEdgeInsetsMake(8.0, 0.0, 24.0, 0.0);

    UINavigationBarAppearance *appearance = [[UINavigationBarAppearance alloc] init];
    [appearance configureWithTransparentBackground];
    appearance.backgroundColor = [UIColor colorWithRed:0.025 green:0.035 blue:0.055 alpha:0.96];
    appearance.titleTextAttributes = @{
        NSForegroundColorAttributeName: UIColor.whiteColor,
        NSFontAttributeName: [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline],
    };
    self.navigationController.navigationBar.standardAppearance = appearance;
    self.navigationController.navigationBar.scrollEdgeAppearance = appearance;
    self.navigationController.navigationBar.compactAppearance = appearance;

    UISwitch *switchAppearance = [UISwitch appearanceWhenContainedInInstancesOfClasses:@[[self class]]];
    switchAppearance.onTintColor = [UIColor colorWithRed:0.24 green:0.78 blue:0.47 alpha:1.0];
    switchAppearance.tintColor = [UIColor colorWithWhite:1.0 alpha:0.24];
}

+ (void)setupViewControllerStyle:(UIViewController *)vc
{
    vc.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    vc.view.backgroundColor = [UIColor colorWithRed:0.025 green:0.035 blue:0.055 alpha:1.0];
    vc.view.layer.cornerRadius = 0;
    vc.view.layer.masksToBounds = NO;
}

- (void)tableView:(UITableView *)tableView willDisplayCell:(UITableViewCell *)cell forRowAtIndexPath:(NSIndexPath *)indexPath
{
    cell.backgroundColor = UIColor.clearColor;
    cell.contentView.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.045];
    cell.textLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    cell.textLabel.textColor = UIColor.whiteColor;
    cell.detailTextLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
    cell.detailTextLabel.textColor = [UIColor colorWithWhite:1.0 alpha:0.60];

    if ([cell isKindOfClass:[UITableViewCell class]]) {
        cell.layer.cornerRadius = 0;
        cell.layer.masksToBounds = YES;
    }
}

- (void)tableView:(UITableView *)tableView willDisplayHeaderView:(UIView *)view forSection:(NSInteger)section
{
    UITableViewHeaderFooterView *header = (UITableViewHeaderFooterView *)view;
    header.textLabel.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightSemibold];
    header.textLabel.textColor = [UIColor colorWithRed:0.30 green:0.90 blue:0.54 alpha:0.95];
    header.textLabel.text = [header.textLabel.text uppercaseString];
    header.contentView.backgroundColor = UIColor.clearColor;
}

- (CGFloat)tableView:(UITableView *)tableView heightForHeaderInSection:(NSInteger)section
{
    return 32.0;
}

- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];
    _table.frame = self.view.bounds;
}

#pragma mark - Status Bar

- (UIStatusBarStyle)preferredStatusBarStyle
{
    return UIStatusBarStyleLightContent;
}

@end
