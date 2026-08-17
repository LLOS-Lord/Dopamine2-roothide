//
//  DOCreditsCell.m
//  Dopamine
//

#import "DOCreditsCell.h"

#define CREDITS_CELL_HEIGHT 44.0

@interface DOCreditsCellItem : UICollectionViewCell
@property (nonatomic, strong) UILabel *label;
@property (nonatomic, strong) NSURL *url;
@end

@implementation DOCreditsCellItem

- (id)initWithFrame:(CGRect)frame
{
    if (self = [super initWithFrame:frame]) {
        self.contentView.backgroundColor = UIColor.clearColor;

        UIView *dot = [[UIView alloc] init];
        dot.backgroundColor = [UIColor colorWithRed:0.30 green:0.90 blue:0.54 alpha:0.95];
        dot.layer.cornerRadius = 2.5;
        dot.translatesAutoresizingMaskIntoConstraints = NO;
        [self.contentView addSubview:dot];

        self.label = [[UILabel alloc] init];
        self.label.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
        self.label.adjustsFontForContentSizeCategory = YES;
        self.label.textColor = [UIColor colorWithWhite:1.0 alpha:0.86];
        self.label.numberOfLines = 1;
        self.label.translatesAutoresizingMaskIntoConstraints = NO;
        [self.contentView addSubview:self.label];

        UIImage *chevronImage = [UIImage systemImageNamed:@"arrow.up.right"];
        UIImageSymbolConfiguration *symbolConfiguration = [UIImageSymbolConfiguration configurationWithPointSize:12 weight:UIImageSymbolWeightMedium];
        UIImageView *chevron = [[UIImageView alloc] initWithImage:[chevronImage imageWithConfiguration:symbolConfiguration]];
        chevron.tintColor = [UIColor colorWithRed:0.30 green:0.90 blue:0.54 alpha:0.95];
        chevron.translatesAutoresizingMaskIntoConstraints = NO;
        [self.contentView addSubview:chevron];

        [NSLayoutConstraint activateConstraints:@[
            [dot.leadingAnchor constraintEqualToAnchor:self.contentView.leadingAnchor constant:4],
            [dot.centerYAnchor constraintEqualToAnchor:self.contentView.centerYAnchor],
            [dot.widthAnchor constraintEqualToConstant:5],
            [dot.heightAnchor constraintEqualToConstant:5],
            [self.label.leadingAnchor constraintEqualToAnchor:dot.trailingAnchor constant:9],
            [self.label.centerYAnchor constraintEqualToAnchor:self.contentView.centerYAnchor],
            [self.label.trailingAnchor constraintLessThanOrEqualToAnchor:chevron.leadingAnchor constant:-8],
            [chevron.trailingAnchor constraintEqualToAnchor:self.contentView.trailingAnchor constant:-4],
            [chevron.centerYAnchor constraintEqualToAnchor:self.contentView.centerYAnchor],
            [chevron.widthAnchor constraintEqualToConstant:16],
        ]];
    }
    return self;
}

- (void)setName:(NSString *)name url:(NSURL *)url
{
    self.label.text = name;
    self.url = url;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    self.alpha = 0.55;
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    [UIView animateWithDuration:0.12 animations:^{ self.alpha = 1.0; }];
    if (self.url) {
        [[UIApplication sharedApplication] openURL:self.url options:@{} completionHandler:nil];
    }
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    [UIView animateWithDuration:0.12 animations:^{ self.alpha = 1.0; }];
}

@end

@interface DOCreditsCell ()
@property (nonatomic, strong) NSArray<NSDictionary *> *names;
@property (nonatomic, strong) UICollectionView *collectionView;
@end

@implementation DOCreditsCell

- (id)initWithSpecifier:(PSSpecifier *)specifier
{
    if (self = [super init]) {
        self.backgroundColor = UIColor.clearColor;
        self.contentView.backgroundColor = UIColor.clearColor;
        self.names = [specifier propertyForKey:@"names"] ?: @[];

        UICollectionViewFlowLayout *layout = [[UICollectionViewFlowLayout alloc] init];
        layout.scrollDirection = UICollectionViewScrollDirectionVertical;
        layout.minimumInteritemSpacing = 12.0;
        layout.minimumLineSpacing = 0.0;
        layout.sectionInset = UIEdgeInsetsZero;

        self.collectionView = [[UICollectionView alloc] initWithFrame:CGRectZero collectionViewLayout:layout];
        self.collectionView.translatesAutoresizingMaskIntoConstraints = NO;
        self.collectionView.backgroundColor = UIColor.clearColor;
        self.collectionView.showsVerticalScrollIndicator = NO;
        self.collectionView.showsHorizontalScrollIndicator = NO;
        [self.collectionView registerClass:[DOCreditsCellItem class] forCellWithReuseIdentifier:@"item"];
        self.collectionView.delegate = self;
        self.collectionView.dataSource = self;
        [self.contentView addSubview:self.collectionView];

        [NSLayoutConstraint activateConstraints:@[
            [self.collectionView.leadingAnchor constraintEqualToAnchor:self.contentView.leadingAnchor constant:16],
            [self.collectionView.trailingAnchor constraintEqualToAnchor:self.contentView.trailingAnchor constant:-16],
            [self.collectionView.topAnchor constraintEqualToAnchor:self.contentView.topAnchor],
            [self.collectionView.bottomAnchor constraintEqualToAnchor:self.contentView.bottomAnchor],
        ]];
    }
    return self;
}

- (NSInteger)collectionView:(UICollectionView *)collectionView numberOfItemsInSection:(NSInteger)section
{
    return self.names.count;
}

- (UICollectionViewCell *)collectionView:(UICollectionView *)collectionView cellForItemAtIndexPath:(NSIndexPath *)indexPath
{
    DOCreditsCellItem *cell = [collectionView dequeueReusableCellWithReuseIdentifier:@"item" forIndexPath:indexPath];
    NSDictionary *entry = self.names[indexPath.item];
    [cell setName:entry[@"name"] url:[NSURL URLWithString:entry[@"link"]]];
    return cell;
}

- (CGSize)collectionView:(UICollectionView *)collectionView layout:(UICollectionViewLayout *)collectionViewLayout sizeForItemAtIndexPath:(NSIndexPath *)indexPath
{
    CGFloat width = floor((collectionView.bounds.size.width - 12.0) / 2.0);
    return CGSizeMake(width, CREDITS_CELL_HEIGHT);
}

- (CGFloat)preferredHeightForWidth:(CGFloat)width
{
    return CREDITS_CELL_HEIGHT * ceil(self.names.count / 2.0);
}

@end
