//
//  GlobalAppearance.h
//  Dopamine
//
//  Created by Lars Fröder on 10.10.23.
//

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import "DOLogViewProtocol.h"

NS_ASSUME_NONNULL_BEGIN

#define UI_IPAD_MAX_WIDTH 600
#define UI_MODAL_PADDING 30
#define UI_PADDING 24
//Action Menu
#define UI_INNER_PADDING 16
#define UI_INNER_PADDING_TINY 10
#define UI_INNER_TOP_PADDING 8
#define UI_ACTION_HEIGHT 70
#define UI_ACTION_HEIGHT_HOME_BTN 62
#define UI_ACTION_HEIGHT_TINY 50

#define SE_PHONE_SIZE_CONST 568

@interface DOUTerminalView : UIView <DOLogViewProtocol>
@property (nonatomic, assign) BOOL compactCommandRows;
- (void)resetWithCommand:(NSString *)command subtitle:(NSString *)subtitle;
- (void)appendPrompt:(NSString *)prompt;
- (void)appendLine:(NSString *)line color:(UIColor *)color;
- (void)appendAttributedLine:(NSAttributedString *)line;
- (void)appendBlankLine;
- (void)appendCommand:(NSString *)command detail:(NSString *)detail enabled:(BOOL)enabled handler:(void (^)(void))handler;
- (void)appendCompactCommand:(NSString *)command enabled:(BOOL)enabled handler:(void (^)(void))handler;
- (void)appendView:(UIView *)view;
- (void)appendBackCommandWithHandler:(void (^)(void))handler;
- (void)beginProgress;
@end

@interface DOGlobalAppearance : NSObject

+ (UIImageSymbolConfiguration *)smallIconImageConfiguration;
+ (UIButtonConfiguration *)defaultButtonConfiguration;
+ (UIButtonConfiguration *)defaultButtonConfigurationWithImagePadding:(CGFloat)imagePadding;
+ (NSAttributedString*)mainSubtitleString:(NSString*)string;
+ (NSAttributedString*)secondarySubtitleString:(NSString*)string;
+ (NSAttributedString*)terminalSubtitleString:(NSString*)string;
+ (BOOL)isHomeButtonDevice;
+ (UIColor*)windowColorWithAlpha:(float)alpha;
+ (BOOL)isRTL;
+ (BOOL)isSmallDevice;
+ (UIColor *)terminalBackgroundColor;
+ (UIColor *)terminalTextColor;
+ (UIColor *)terminalMutedColor;
+ (UIColor *)terminalAccentColor;
+ (UIColor *)terminalRuleColor;

@end

NS_ASSUME_NONNULL_END
