//
//  DOModalTransitionPush.m
//  Dopamine
//

#import "DOModalTransitionPush.h"

@interface DOModalTransitionPush ()
@property (nonatomic, assign) BOOL forwards;
@end

@implementation DOModalTransitionPush

- (id)initForwards:(BOOL)forwards
{
    self = [super init];
    if (self) _forwards = forwards;
    return self;
}

- (NSTimeInterval)transitionDuration:(nullable id<UIViewControllerContextTransitioning>)transitionContext
{
    return 0.0;
}

- (void)animateTransition:(id<UIViewControllerContextTransitioning>)transitionContext
{
    UIViewController *toViewController = [transitionContext viewControllerForKey:UITransitionContextToViewControllerKey];
    UIView *container = transitionContext.containerView;
    toViewController.view.frame = container.bounds;
    [container addSubview:toViewController.view];
    [transitionContext completeTransition:!transitionContext.transitionWasCancelled];
}

@end
