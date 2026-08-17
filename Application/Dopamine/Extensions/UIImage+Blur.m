//
//  UIImage+Blur.m
//  Dopamine
//
//  Created by Lars Fröder on 01.10.23.
//

#import <Foundation/Foundation.h>
#import "UIImage+Blur.h"
#import <CoreImage/CoreImage.h>

@implementation UIImage (Blur)

- (instancetype)imageWithBlur:(float)radius
{
    if (!self.CGImage) return nil;

    CIImage *ciImage = [CIImage imageWithCGImage:self.CGImage];
    CIFilter *filter = [CIFilter filterWithName:@"CIGaussianBlur"];
    if (!ciImage || !filter) return nil;
    [filter setDefaults];
    [filter setValue:[ciImage imageByClampingToExtent] forKey:kCIInputImageKey];
    [filter setValue:@(radius) forKey:kCIInputRadiusKey];

    CIImage *filteredImage = filter.outputImage;
    CIImage *outputImage = filteredImage ? [filteredImage imageByCroppingToRect:ciImage.extent] : nil;
    if (!outputImage) return nil;

    CIContext *context = [CIContext contextWithOptions:nil];
    CGImageRef cgImg = [context createCGImage:outputImage fromRect:ciImage.extent];
    if (!cgImg) return nil;

    UIImage *image = [UIImage imageWithCGImage:cgImg scale:self.scale orientation:self.imageOrientation];
    CGImageRelease(cgImg);
    return image;
}

- (instancetype)imageWithHue:(float)hue
{
    if (!self.CGImage) return nil;

    CIImage *ciImage = [CIImage imageWithCGImage:self.CGImage];
    CIFilter *filter = [CIFilter filterWithName:@"CIHueAdjust"];
    if (!ciImage || !filter) return nil;
    [filter setDefaults];
    [filter setValue:ciImage forKey:kCIInputImageKey];
    [filter setValue:@(hue) forKey:kCIInputAngleKey];

    CIImage *outputImage = filter.outputImage;
    if (!outputImage) return nil;

    CIContext *context = [CIContext contextWithOptions:nil];
    CGImageRef cgImg = [context createCGImage:outputImage fromRect:ciImage.extent];
    if (!cgImg) return nil;

    UIImage *image = [UIImage imageWithCGImage:cgImg scale:self.scale orientation:self.imageOrientation];
    CGImageRelease(cgImg);
    return image;
}

@end
