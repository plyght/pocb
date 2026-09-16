#include "MacIntegration.hpp"
#include "MacInternal.hpp"

#include <algorithm>

#ifdef __APPLE__
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/message.h>
#import <objc/runtime.h>

namespace {

NSObject *makeVariableBlurFilter() {
    Class filterClass = NSClassFromString(@"CAFilter");
    SEL factory = NSSelectorFromString(@"filterWithType:");
    if (!filterClass || ![filterClass respondsToSelector:factory]) return nil;
    return ((id(*)(id, SEL, id))objc_msgSend)(filterClass, factory, @"variableBlur");
}

CALayer *makeBackdropLayer() {
    Class backdropClass = NSClassFromString(@"CABackdropLayer");
    if (!backdropClass || ![backdropClass isSubclassOfClass:CALayer.class]) return nil;
    return [[backdropClass alloc] init];
}

CGImageRef makeLinearMask(double offset, double interpolation) {
    const size_t side = 256;
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(nullptr, side, side, 8, 0, space, kCGImageAlphaPremultipliedLast);
    if (!ctx) {
        CGColorSpaceRelease(space);
        return nullptr;
    }
    const double start = std::min(std::max(offset, 0.0), 1.0);
    const double end = std::max(start + std::max(interpolation, 0.0), start + 1e-6);
    const CGFloat comps[8] = {1, 1, 1, 1, 1, 1, 1, 0};
    const CGFloat locs[2] = {(CGFloat)start, (CGFloat)std::min(end, 1.0)};
    CGGradientRef gradient = CGGradientCreateWithColorComponents(space, comps, locs, 2);
    CGContextDrawLinearGradient(ctx, gradient,
                                CGPointMake(0, side), CGPointMake(0, 0),
                                kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
    CGGradientRelease(gradient);
    CGImageRef image = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    CGColorSpaceRelease(space);
    return image;
}

}  // namespace

@interface PocbGlurBackdropView : NSView
- (instancetype)initWithFrame:(NSRect)frame radius:(CGFloat)radius offset:(double)offset interpolation:(double)interpolation;
@end

@implementation PocbGlurBackdropView {
    CALayer *_backdrop;
    NSObject *_filter;
}

- (instancetype)initWithFrame:(NSRect)frame radius:(CGFloat)radius offset:(double)offset interpolation:(double)interpolation {
    if (!(self = [super initWithFrame:frame])) return nil;
    self.wantsLayer = YES;
    self.layer.masksToBounds = YES;
    self.layer.backgroundColor = NSColor.clearColor.CGColor;
    _filter = makeVariableBlurFilter();
    _backdrop = makeBackdropLayer();
    if (!_filter || !_backdrop) {
        NSLog(@"[pocb] variable blur backdrop unavailable");
        return self;
    }
    CGImageRef mask = makeLinearMask(offset, interpolation);
    [_filter setValue:@(radius) forKey:@"inputRadius"];
    [_filter setValue:(__bridge id)mask forKey:@"inputMaskImage"];
    [_filter setValue:@YES forKey:@"inputNormalizeEdges"];
    if (mask) CGImageRelease(mask);
    _backdrop.filters = @[_filter];
    _backdrop.frame = self.bounds;
    [self.layer addSublayer:_backdrop];
    return self;
}

- (BOOL)isOpaque { return NO; }
- (NSView *)hitTest:(NSPoint)point { (void)point; return nil; }

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    if (!self.window || !_backdrop) return;
    [_backdrop setValue:@(self.window.backingScaleFactor) forKey:@"scale"];
}

- (void)layout {
    [super layout];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _backdrop.frame = self.bounds;
    [CATransaction commit];
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _backdrop.frame = self.bounds;
    [CATransaction commit];
}

@end

namespace mac {

NSView *makeGlurBackdropView(NSRect frame, double cornerRadius, double blurRadius, double offset, double interpolation) {
    PocbGlurBackdropView *view = [[PocbGlurBackdropView alloc] initWithFrame:frame
                                                                      radius:blurRadius
                                                                      offset:offset
                                                                interpolation:interpolation];
    view.layer.cornerRadius = cornerRadius;
    [view.layer setValue:@"continuous" forKey:@"cornerCurve"];
    return view;
}

}  // namespace mac
#endif
