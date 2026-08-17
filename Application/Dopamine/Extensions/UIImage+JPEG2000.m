#import <ImageIO/ImageIO.h>
#import <MobileCoreServices/MobileCoreServices.h>
#import <Foundation/Foundation.h>
#import "UIImage+JPEG2000.h"

@implementation UIImage (JPEG2000) 

- (NSData *)jp2DataWithCompressionQuality:(CGFloat)quality
{
	if (!self.CGImage) return nil;

	NSMutableData *data = [NSMutableData data];
	CGImageDestinationRef destination = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)data, kUTTypeJPEG2000, 1, NULL);
	if (!destination) return nil;

	NSDictionary *options = @{
		(NSString *)kCGImageDestinationLossyCompressionQuality: @(quality)
	};

	CGImageDestinationAddImage(destination, self.CGImage, (__bridge CFDictionaryRef)options);
	BOOL finalized = CGImageDestinationFinalize(destination);
	CFRelease(destination);
	if (!finalized || data.length == 0) return nil;
	return data;
}

@end