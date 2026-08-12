//
//  DOThemeManager.m
//  Dopamine
//
//  Created by tomt000 on 14/02/2024.
//

#import "DOThemeManager.h"
#import "DOPreferenceManager.h"

@implementation DOThemeManager

+ (instancetype)sharedInstance
{
    static DOThemeManager *sharedManager = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedManager = [[DOThemeManager alloc] init];
    });
    return sharedManager;
}

- (id)init
{
    self = [super init];
    if (self) {
        self.themes = [[NSMutableArray alloc] init];

        NSString *path = [[NSBundle mainBundle] pathForResource:@"Themes" ofType:@"plist"];
        NSArray *themes = [NSArray arrayWithContentsOfFile:path];

        for (id theme in themes) {
            if (![theme isKindOfClass:[NSDictionary class]]) {
                continue;
            }

            DOTheme *newTheme = [[DOTheme alloc] initWithDictionary:theme];
            [((NSMutableArray *)self.themes) addObject:newTheme];
        }

        if (self.themes.count == 0) {
            DOTheme *fallbackTheme = [[DOTheme alloc] initWithDictionary:@{
                @"key": @"default",
                @"name": @"Dopamine",
                @"image": @"Background_Green",
                @"actionMenuColor": @"723f3f3f",
                @"windowColor": @"994c4c4c",
                @"blur": @18,
                @"titleShadow": @NO
            }];
            [((NSMutableArray *)self.themes) addObject:fallbackTheme];
        }

    }
    return self;
}

- (NSArray*)getAvailableThemeKeys
{
    NSMutableArray *keys = [[NSMutableArray alloc] init];
    for (DOTheme *theme in _themes) {
        [keys addObject:theme.key];
    }
    return keys;
}

- (NSArray*)getAvailableThemeNames
{
    NSMutableArray *names = [[NSMutableArray alloc] init];
    for (DOTheme *theme in _themes) {
        [names addObject:theme.name];
    }
    return names;
}

- (DOTheme*)getThemeForKey:(NSString*)key
{
    for (DOTheme *theme in _themes) {
        if ([theme.key isEqualToString:key]) {
            return theme;
        }
    }
    return nil;
}

- (DOTheme*)enabledTheme
{
    id value = [[DOPreferenceManager sharedManager] preferenceValueForKey:@"theme"];
    if (![value isKindOfClass:[NSString class]])
        return self.themes.firstObject;
    return [self getThemeForKey:value] ?: self.themes.firstObject;
}


+ (UIColor*)menuColorWithAlpha:(float)alpha
{
    DOTheme *theme = [[DOThemeManager sharedInstance] enabledTheme];

    UIColor *color = theme.actionMenuColor;
    CGFloat red = 0, green = 0, blue = 0, currentAlpha = 0;
    if (![color getRed:&red green:&green blue:&blue alpha:&currentAlpha]) {
        color = [UIColor colorWithWhite:0 alpha:0.45];
        [color getWhite:&red alpha:&currentAlpha];
        green = red;
        blue = red;
    }
    return [UIColor colorWithRed:red green:green blue:blue alpha:currentAlpha * alpha];
}


@end
