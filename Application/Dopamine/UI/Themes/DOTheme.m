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
    unsigned int hexInt = 0;
    NSScanner *scanner = [NSScanner scannerWithString:hexString];
    [scanner scanHexInt:&hexInt];
    return [UIColor colorWithRed:((CGFloat)((hexInt & 0xFF0000) >> 16))/255.0 green:((CGFloat)((hexInt & 0xFF00) >> 8))/255.0 blue:((CGFloat)(hexInt & 0xFF))/255.0 alpha:((CGFloat)((hexInt & 0xFF000000) >> 24))/255.0];
}

- (UIImage *)image
{
    if (_image == nil) {
        UIImage *sourceImage = [UIImage imageNamed:self.imageName];
        if (!sourceImage && self.imageName.pathExtension.length > 0) {
            sourceImage = [UIImage imageNamed:self.imageName.stringByDeletingPathExtension];
        }
        if (!sourceImage || !sourceImage.CGImage) return nil;

        UIImage *blurredImage = [sourceImage imageWithBlur:self.blur];
        _image = blurredImage ?: sourceImage;
    }
    return _image;
}

- (UIImage *)generateBootLogo
{
    UIImage *backgroundImage = [self image];
    UIImage *overlayImage = [UIImage imageNamed:@"DopamineLogo"];
    if (!backgroundImage || !backgroundImage.CGImage || !overlayImage || !overlayImage.CGImage) {
        return nil;
    }

    CGSize canvasSize = backgroundImage.size;
    if (canvasSize.width <= 0.0 || canvasSize.height <= 0.0) return nil;

    CGSize overlaySize = CGSizeMake(350, 350);
    CGPoint overlayOrigin = CGPointMake((canvasSize.width - overlaySize.width) / 2.0,
                                        (canvasSize.height - overlaySize.height) / 2.0);
    CGFloat scale = backgroundImage.scale > 0.0 ? backgroundImage.scale : 1.0;

    UIGraphicsBeginImageContextWithOptions(canvasSize, NO, scale);
    if (!UIGraphicsGetCurrentContext()) return nil;

    [backgroundImage drawInRect:CGRectMake(0, 0, canvasSize.width, canvasSize.height)];
    [overlayImage drawInRect:CGRectMake(overlayOrigin.x, overlayOrigin.y, overlaySize.width, overlaySize.height)];

    UIImage *finalImage = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return finalImage;
}

@end
