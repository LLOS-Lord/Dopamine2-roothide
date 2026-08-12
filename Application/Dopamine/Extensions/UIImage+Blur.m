//
//  UIImage+Blur.m
//  Dopamine
//
//  Created by Lars Fröder on 01.10.23.
//

#import <Foundation/Foundation.h>
#import "UIImage+Blur.h"
#import <CoreImage/CoreImage.h>
#import <CoreGraphics/CoreGraphics.h>

@implementation UIImage (Blur)

- (instancetype)imageWithBlur:(float)radius
{
    if (!self || !self.CGImage || radius <= 0) {
        return self;
    }

    CIImage *ciImage = [CIImage imageWithCGImage:self.CGImage];
    if (!ciImage) {
        return self;
    }

    CIFilter *filter = [CIFilter filterWithName:@"CIGaussianBlur"];
    if (!filter) {
        return self;
    }

    [filter setDefaults];
    [filter setValue:[ciImage imageByClampingToExtent] forKey:kCIInputImageKey];
    [filter setValue:@(radius) forKey:kCIInputRadiusKey];

    CIImage *filteredImage = [filter outputImage];
    if (!filteredImage) {
        return self;
    }

    CIImage *outputImage = [filteredImage imageByCroppingToRect:[ciImage extent]];
    CIContext *context   = [CIContext contextWithOptions:nil];
    if (!context || !outputImage) {
        return self;
    }

    CGImageRef cgImg     = [context createCGImage:outputImage fromRect:[ciImage extent]];
    if (!cgImg) {
        return self;
    }

    UIImage *blurredImage = [UIImage imageWithCGImage:cgImg scale:self.scale orientation:self.imageOrientation];
    CGImageRelease(cgImg);
    return blurredImage ?: self;
}

- (instancetype)imageWithHue:(float)hue
{
    if (!self || !self.CGImage) {
        return self;
    }

    CIImage *ciImage = [CIImage imageWithCGImage:self.CGImage];
    if (!ciImage) {
        return self;
    }

    CIFilter *filter = [CIFilter filterWithName:@"CIHueAdjust"];
    if (!filter) {
        return self;
    }

    [filter setDefaults];
    [filter setValue:ciImage forKey:kCIInputImageKey];
    [filter setValue:@(hue) forKey:kCIInputAngleKey];

    CIImage *outputImage = [filter outputImage];
    if (!outputImage) {
        return self;
    }

    CIContext *context   = [CIContext contextWithOptions:nil];
    if (!context) {
        return self;
    }

    CGImageRef cgImg     = [context createCGImage:outputImage fromRect:[ciImage extent]];
    if (!cgImg) {
        return self;
    }

    UIImage *adjustedImage = [UIImage imageWithCGImage:cgImg scale:self.scale orientation:self.imageOrientation];
    CGImageRelease(cgImg);
    return adjustedImage ?: self;
}

@end
