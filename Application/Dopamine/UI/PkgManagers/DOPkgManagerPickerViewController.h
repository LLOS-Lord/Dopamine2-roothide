//
//  DOPkgManagerPickerViewController.h
//  Dopamine
//
//  Created by tomt000 on 11/02/2024.
//

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface DOPkgManagerPickerViewController : UIViewController

- (instancetype)initWithCompletion:(void (^)(void))completion;

@end

NS_ASSUME_NONNULL_END
