//
//  DOTheme.m
//  Dopamine
//
//  Created by tomt000 on 14/02/2024.
//

#import "DOTheme.h"
#import "UIImage+Blur.h"

@interface DOTheme ()
@property (nonatomic, retain) NSString *imageName;
@end

@implementation DOTheme

- (id)initWithDictionary: (NSDictionary *)dictionary
{
    self = [super init];
    if (self) {
        self.name = [dictionary objectForKey:@"name"];
        self.icon = [dictionary objectForKey:@"icon"];
        self.key = [dictionary objectForKey:@"key"];
        self.imageName = [dictionary objectForKey:@"image"];
        self.windowColor = [self colorFromHexString:[dictionary objectForKey:@"windowColor"]];
        self.actionMenuColor = [self colorFromHexString:[dictionary objectForKey:@"actionMenuColor"]];
        self.blur = [[dictionary objectForKey:@"blur"] floatValue];
        self.titleShadow = [[dictionary objectForKey:@"titleShadow"] boolValue];
    }
    return self;
}

- (UIColor*)colorFromHexString:(NSString*)hexString
{
    if (![hexString isKindOfClass:[NSString class]] || hexString.length == 0) {
        return [UIColor clearColor];
    }

    unsigned int hexInt = 0;
    NSScanner *scanner = [NSScanner scannerWithString:hexString];
    [scanner scanHexInt:&hexInt];
    return [UIColor colorWithRed:((CGFloat)((hexInt & 0xFF0000) >> 16))/255.0 green:((CGFloat)((hexInt & 0xFF00) >> 8))/255.0 blue:((CGFloat)(hexInt & 0xFF))/255.0 alpha:((CGFloat)((hexInt & 0xFF000000) >> 24))/255.0];
}

- (UIImage *)image
{
    if (_image == nil) {
        NSString *assetName = self.imageName;
        if (assetName.pathExtension.length > 0) {
            assetName = [assetName stringByDeletingPathExtension];
        }

        UIImage *baseImage = assetName.length > 0 ? [UIImage imageNamed:assetName] : nil;
        if (!baseImage && ![assetName isEqualToString:@"Background_Green"]) {
            baseImage = [UIImage imageNamed:@"Background_Green"];
        }

        if (baseImage && self.blur > 0) {
            _image = [baseImage imageWithBlur:self.blur] ?: baseImage;
        }
        else {
            _image = baseImage;
        }
    }

    return _image;
}

- (UIImage *)generateBootLogo
{
    UIImage *backgroundImage = [self image];
    if (!backgroundImage) {
        return [UIImage imageNamed:@"DopamineLogo"];
    }

    CGSize canvasSize = backgroundImage.size;

    UIImage *overlayImage = [UIImage imageNamed:@"DopamineLogo"];
    if (!overlayImage) {
        return backgroundImage;
    }

    CGSize overlaySize = CGSizeMake(350, 350);
    CGPoint overlayOrigin = CGPointMake((canvasSize.width - overlaySize.width) / 2.0,
                                        (canvasSize.height - overlaySize.height) / 2.0);

    CGFloat scale = backgroundImage.scale > 0 ? backgroundImage.scale : [UIScreen mainScreen].scale;
    UIGraphicsBeginImageContextWithOptions(canvasSize, NO, scale);

    [backgroundImage drawInRect:CGRectMake(0, 0, canvasSize.width, canvasSize.height)];

    // Render overlay (Dopamine Logo) in center of background for boot logo
    [overlayImage drawInRect:CGRectMake(overlayOrigin.x, overlayOrigin.y, overlaySize.width, overlaySize.height)];

    UIImage *finalImage = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    return finalImage;
}

@end
