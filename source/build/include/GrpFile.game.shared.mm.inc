// Shared macOS GRP entry wrapper used by Duke3D-family startup windows.

@implementation GrpFile
- (id)initWithGrpfile:(grpfile_t const *)grpfile
{
    self = [super init];
    if (self) {
        fg = grpfile;
        namestring = [NSString stringWithCString:fg->type->name encoding:NSUTF8StringEncoding];
        [namestring retain];
        grpnamestring = [NSString stringWithCString:fg->filename encoding:NSUTF8StringEncoding];
        [grpnamestring retain];
    }
    return self;
}
- (void)dealloc
{
    [namestring release];
    [grpnamestring release];
    [super dealloc];
}
- (NSString *)name
{
    return namestring;
}
- (NSString *)grpname
{
    return grpnamestring;
}
- (grpfile_t const *)entryptr
{
    return fg;
}
@end
