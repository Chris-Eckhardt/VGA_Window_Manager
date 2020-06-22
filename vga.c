#include <vga.h>



/* designated base vga memory address */
#define VIDEO_BASE_ADDRESS  	0xA0000

/* screen dimentions (320x200) */
#define SCREEN_WIDTH        	320  
#define SCREEN_HEIGHT       	200

/* values to be written to vga registers */
#define VGA_AC_INDEX       	    0x3C0
#define VGA_AC_WRITE        	0x3C0
#define VGA_AC_READ         	0x3C1
#define VGA_MISC_WRITE      	0x3C2
#define VGA_SEQ_INDEX       	0x3C4
#define VGA_SEQ_DATA        	0x3C5
#define VGA_DAC_READ_INDEX  	0x3C7
#define VGA_DAC_WRITE_INDEX 	0x3C8
#define VGA_DAC_DATA        	0x3C9
#define VGA_MISC_READ       	0x3CC
#define VGA_GC_INDEX        	0x3CE
#define VGA_GC_DATA         	0x3CF
#define VGA_CRTC_INDEX      	0x3D4       
#define VGA_CRTC_DATA       	0x3D5
#define VGA_INSTAT_READ     	0x3DA

/* used for indexing VGA REGISTERS*/
#define VGA_NUM_SEQ_REGS    	5
#define VGA_NUM_CRTC_REGS   	25
#define VGA_NUM_GC_REGS     	9
#define VGA_NUM_AC_REGS     	21
#define VGA_NUM_REGS        	(1 + VGA_NUM_SEQ_REGS + VGA_NUM_CRTC_REGS + \
				VGA_NUM_GC_REGS + VGA_NUM_AC_REGS)

#define BLACK 			0x00
#define WHITE 			0x3F

#define FONT_SIZE 8

int current_color = 0x01;


int g_window_id = 0;

typedef struct _BOUND {
	int x;
    int y;
    int width;
    int height;
} BOUND;

typedef struct _QNODE {
	int children;
	int hidden;
	BOUND bound;
	struct _QNODE * nw;
	struct _QNODE * ne;
	struct _QNODE * sw;
	struct _QNODE * se;
} QNODE;

typedef struct _FRAME {
	BOUND bound;
	char * title;
} FRAME;

typedef struct _CANVAS {
	BOUND bound;
	int * buffer;
} CANVAS;

typedef struct _VGA_WINDOW {
	int id;
	FRAME frame;
	CANVAS canvas;
    int color;
	QNODE * root;
	struct _VGA_WINDOW * next;
    struct _VGA_WINDOW * prev;
} VGA_WINDOW;

VGA_WINDOW * window_list_head;
VGA_WINDOW * window_list_tail;

PORT vga_port;

/***************************************************************
 *                     Function Prototypes                     *
 ***************************************************************/

/* vga driver functions */

void vga_process ();

void write_regs (unsigned char * regs);

void create_window ( PARAM_VGA_CREATE_WINDOW * params);

void draw_pixel (PARAM_VGA_DRAW_PIXEL * params);

void draw_line (PARAM_VGA_DRAW_LINE * params);

void draw_text (PARAM_VGA_DRAW_TEXT * params);

void change_window(PARAM_VGA_CHANGE_FOCUS * params);

void clear_screen();

void set_pixel (VGA_WINDOW * window, int x, int y, int color);

void poke_pixel (int x, int y, int color);

void vga_draw_canvas(VGA_WINDOW * window);

void vga_draw_frame(VGA_WINDOW * window);

void vga_draw_window(VGA_WINDOW * window);

void vga_render_windows();

int get_canvas_pixel(VGA_WINDOW * wnd, int x, int y);

void set_canvas_pixel(VGA_WINDOW * wnd, int x, int y, int color);

VGA_WINDOW * get_window(int id);

void draw_character (VGA_WINDOW * wnd, int x, int y, int bg_color, int fg_color, char c);

void draw_string (VGA_WINDOW * wnd, int x, int y, int bg_color, int fg_color, const char * str);

int m_abs (int a);

/* quadtree functions */

BOUND create_bound(int x, int y, int width, int height);

int bound_contains(BOUND * b, int x, int y);

int bound_intersects(BOUND * a, BOUND * b);

QNODE * create_qnode(int x, int y, int width, int height);

QNODE * destroy_qnode(QNODE * q);

void destroy_children(QNODE * q);

void check_qnode(QNODE * q, BOUND b);

void subdivide(QNODE * q, BOUND b);

int search_qtree(QNODE * q, int x, int y);

BOUND get_intersection(BOUND * a, BOUND * b);

int bound_contains_within(BOUND * in, BOUND * out);

void add_window_to_list(VGA_WINDOW * w);

void build_quadtrees();

/***************************************************************
 *                          INIT VGA                           *
 ***************************************************************/

int init_vga()
{

    unsigned char g_320x200x256[] =
    {
        /* MISC */
        0x63,
        /* SEQ */
        0x03, 0x01, 0x0F, 0x00, 0x0E,
        /* CRTC */
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
        0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
        0xFF,
        /* GC */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F,
        0xFF,
        /* AC */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x41, 0x00, 0x0F, 0x00, 0x00
    };

    /* set to vga 256 color mode */
    write_regs(g_320x200x256);

    /* create vga driver process */
    vga_port = create_process(vga_process, 5, 0, "VGA");

    /* get rid of junk in memory */
    clear_screen();

    /* ret */
    return 1;
}

/********************************************************************************
 *                              VGA DEVICE DRIVER                               *
 * *****************************************************************************/ 

void vga_process (PROCESS proc, PARAM param)
{

    VGA_WINDOW_MSG * msg;
    PROCESS sender;

    /* wait for messages */
    while (1)
    {
        
        /* get message from sending process */
        msg = (VGA_WINDOW_MSG *) receive(&sender);

        /* process request from message */
        switch (msg->cmd)
        {

            case VGA_CREATE_WINDOW:
                create_window( (PARAM_VGA_CREATE_WINDOW *) &msg->u.create_window );
                break;

            case VGA_DRAW_TEXT:
                draw_text( (PARAM_VGA_DRAW_TEXT *) &msg->u.draw_text );
                break;

            case VGA_DRAW_PIXEL:
                draw_pixel( (PARAM_VGA_DRAW_PIXEL *) &msg->u.draw_pixel );
                break;

            case VGA_DRAW_LINE:
                draw_line( (PARAM_VGA_DRAW_LINE *) &msg->u.draw_line );
                break;

            case VGA_CHANGE_FOCUS:
                change_window( (PARAM_VGA_CHANGE_FOCUS *) &msg->u.change_focus );
                break;
        }

        /* reply to sender */
        reply(sender);
    }
}

/**************************************************************
 *               WRITE TO THE VGA REGISTERS                   *
 *************************************************************/

void write_regs (unsigned char * regs)
{
    unsigned int i;

    /* write MISCELLANEOUS reg */
    outportb (VGA_MISC_WRITE, *(regs++));
    
    /* write SEQUENCER regs */
    for (i = 0; i < VGA_NUM_SEQ_REGS; i++)
    {
        outportb (VGA_SEQ_INDEX, i);
        outportb (VGA_SEQ_DATA, *(regs++));
    }

    /* unlock CRTC registers */
    outportb (VGA_CRTC_INDEX, 0x03);
    outportb (VGA_CRTC_DATA, inportb (VGA_CRTC_DATA) | 0x80);
    outportb (VGA_CRTC_INDEX, 0x11);
    outportb (VGA_CRTC_DATA, inportb (VGA_CRTC_DATA) & ~0x80);

    /* make sure they remain unlocked */
    regs[0x03] |= 0x80;
    regs[0x11] &= ~0x80;

    /* write CRTC regs */
    for (i = 0; i < VGA_NUM_CRTC_REGS; i++)
    {
        outportb (VGA_CRTC_INDEX, i);
        outportb (VGA_CRTC_DATA, *(regs++));
    }

    /* write GRAPHICS CONTROLLER regs */
    for (i = 0; i < VGA_NUM_GC_REGS; i++)
    {
        outportb (VGA_GC_INDEX, i);
        outportb (VGA_GC_DATA, *(regs++));
    }

    /* write ATTRIBUTE CONTROLLER regs */
    for (i = 0; i < VGA_NUM_AC_REGS; i++)
    {
        (void) inportb (VGA_INSTAT_READ);
        outportb (VGA_AC_INDEX, i);
        outportb (VGA_AC_WRITE, *(regs++));
    }

    /* lock 16-color palette and unblank display */
    (void) inportb (VGA_INSTAT_READ);
    outportb (VGA_AC_INDEX, 0x20);
}

/**************************************************************
 *                   API : CREATE WINDOW                      *
 *************************************************************/

void create_window ( PARAM_VGA_CREATE_WINDOW * params)
{
	VGA_WINDOW * window = malloc( sizeof(VGA_WINDOW) );
	window->id = g_window_id++;
	params->window_id = window->id;
	window->frame.bound.x = params->x-1;
	window->frame.bound.y = params->y-10;
	window->frame.bound.width = params->width+2;
	window->frame.bound.height = params->height+11;
	window->canvas.bound.x = params->x;
	window->canvas.bound.y = params->y;
	window->canvas.bound.width = params->width;
	window->canvas.bound.height = params->height;
    window->canvas.buffer = malloc(
        sizeof(int) * (window->canvas.bound.width) * (window->canvas.bound.height));
    window->color = current_color++;

    int bound_size = 1;
    while (bound_size < window->frame.bound.width ||
        bound_size < window->frame.bound.height) 
        {
            bound_size *= 2;
        }

	window->root = create_qnode(
        window->frame.bound.x, 
        window->frame.bound.y, 
        bound_size, 
        bound_size);

    window->next = NULL;
    window->prev = NULL;
	add_window_to_list(window);
    build_quadtrees();
    vga_render_windows();
}

/**************************************************************
 *                    API : DRAW PIXEL                        *
 *************************************************************/

void draw_pixel (PARAM_VGA_DRAW_PIXEL * params)
{
    VGA_WINDOW * wnd = get_window(params->window_id);
	if(wnd == NULL)
		return;

	set_canvas_pixel(wnd, params->x, params->y, params->color);

    vga_draw_canvas(wnd);
}

 /*************************************************************
 *                      API : DRAW TEXT                       * 
 *************************************************************/

void draw_text (PARAM_VGA_DRAW_TEXT * params) 
{
    VGA_WINDOW * wnd = get_window(params->window_id);
	if(wnd == NULL)
		return;

	/* print string */
	draw_string(wnd, params->x, params->y, params->bg_color, params->fg_color, params->text);
    vga_draw_canvas(wnd);
}

 /*************************************************************
 *                     API : DRAW LINE                        *
 *************************************************************/

void draw_line (PARAM_VGA_DRAW_LINE * params)
{

    VGA_WINDOW * wnd = get_window(params->window_id);
	if(wnd == NULL)
		return;

	int x, y, dx, dy, dx1, dy1, px, py, xe, ye, i;

	dx = params->x1 - params->x0;
	dy = params->y1 - params->y0;

	dx1 = m_abs(dx);
	dy1 = m_abs(dy);

	px = 2 * dy1 - dx1;
	py = 2 * dx1 - dy1;

	if (dy1 <= dx1) {

		if (dx >= 0) {
			x = params->x0;
			y = params->y0;
			xe = params->x1;
		} else {
			x = params->x1;
			y = params->y1;
			xe = params->x0;
		}

		set_canvas_pixel(wnd, x, y, params->color);

		for (i = 0; x < xe; i++) {
			x += 1;

			if (px < 0) {
				px = px + 2 * dy1;
			} else {
				if ((dx < 0 && dy < 0) || (dx > 0 && dy > 0)) {
					y += 1;
				} else {
					y -= 1;
				}
				px = px + 2 * (dy1 - dx1);
			}

			set_canvas_pixel(wnd, x, y, params->color);
		}

	} else {

		if (dy >= 0) {
			x = params->x0;
			y = params->y0;
			ye = params->y1;
		} else {
			x = params->x1;
			y = params->y1;
			ye = params->y0;
		}

		set_canvas_pixel(wnd, x, y, params->color);

		for (i = 0; y < ye; i++) {
			y += 1;

			if (py < 0) {
				py = py + 2 * dx1;
			} else {
				if ((dx < 0 && dy < 0) || (dx > 0 && dy > 0)) {
					x += 1;
				} else {
					x -= 1;
				}
				py = py + 2 * (dx1 - dy1);
			}

            set_canvas_pixel(wnd, x, y, params->color);
			
		}

	}
    vga_draw_canvas(wnd);
}

 /*************************************************************
 *                     API : CHANGE WINDOW                    *
 *************************************************************/

 void change_window(PARAM_VGA_CHANGE_FOCUS * params)
 {
    bring_window_forward(params->window_id);
    build_quadtrees();
    vga_render_windows();
 }

/**************************************************************
 *                 PIXEL DRAWING UTILITIES                    *
 *************************************************************/

void clear_screen()
{
	for(int x = 0; x < SCREEN_WIDTH; x++)
		for(int y = 0; y < SCREEN_HEIGHT; y++)
			poke_pixel(x, y, BLACK);
}

void set_pixel (VGA_WINDOW * window, int x, int y, int color)
{
	// check screen boundary

	// check quadtree
	if(search_qtree(window->root, x, y) == 0)
		poke_pixel(x, y, color);
}

void poke_pixel (int x, int y, int color)
{
	poke_b( (VIDEO_BASE_ADDRESS + y * SCREEN_WIDTH + x), color);
}

int m_abs (int a) 
{
	return a < 0 ? -a : a;
}

void draw_string (VGA_WINDOW * wnd, int x, int y, int bg_color, int fg_color, const char * str) {

	while (*(str) != '\0') {
		draw_character(wnd, x, y, bg_color, fg_color, *(str));
		str++;
		x+=8;
	}
}

void draw_character (VGA_WINDOW * wnd, int x, int y, int bg_color, int fg_color, char c) {

	int i, n;
	unsigned char b;

	for (i = 0; i < FONT_SIZE; i++) {

		b = g_8x8_font[(((int)c)*FONT_SIZE)+i];

		for (n = 0; n < FONT_SIZE; n++) {

			if ((b>>n&1)) {
				set_canvas_pixel(wnd, x+FONT_SIZE-n-1, y+i, fg_color);
			} else {
				set_canvas_pixel(wnd, x+FONT_SIZE-n-1, y+i, bg_color);
			}
		}
	}
}


/**************************************************************
 *               WINDOW MANAGEMENT FUNCTIONS                  *
 *************************************************************/

VGA_WINDOW * get_window(int id)
{
    VGA_WINDOW * w_ptr = window_list_head;
    while(w_ptr != NULL) {
        if(w_ptr->id == id)
            break;
        w_ptr = w_ptr->next;
    }
    return w_ptr;
}

void vga_render_windows()
{
    clear_screen();
    if(!window_list_head)
        return;

    VGA_WINDOW * w_ptr = window_list_head;
    while(w_ptr != NULL) {
        vga_draw_window(w_ptr);
        w_ptr = w_ptr->next;
    }
}

void vga_draw_frame(VGA_WINDOW * window)
{
	BOUND fb = window->frame.bound;

	for(int x = fb.x; x < fb.x+fb.width; x++)
		for(int y = fb.y; y < fb.y+10; y++)
			set_pixel(window, x, y, WHITE);
    
    for(int y = fb.y; y < fb.y+fb.height; y++)
        set_pixel(window, fb.x, y, WHITE);

    for(int y = fb.y; y < fb.y+fb.height; y++)
        set_pixel(window, fb.x+fb.width-1, y, WHITE);

    for(int x = fb.x; x < fb.x+fb.width; x++)
        set_pixel(window, x, fb.y+fb.height-1, WHITE);

    draw_frame_title(window);
}

void vga_draw_canvas(VGA_WINDOW * window)
{
	BOUND cb = window->canvas.bound;

	for(int x = 0; x < cb.width; x++)
		for(int y = 0; y < cb.height; y++)
			set_pixel(window, x+cb.x, y+cb.y, get_canvas_pixel(window, x, y));
        
}

void vga_draw_window(VGA_WINDOW * window)
{
	vga_draw_frame(window);
	vga_draw_canvas(window);
}

/* used to add a new window to the global window list */
void add_window_to_list(VGA_WINDOW * w)
{
    if(!window_list_head) {
        window_list_head = w;
        window_list_tail = w;
    } else {
        w->next = window_list_head;
        window_list_head->prev = w;
        window_list_head = w;
    }
}

int get_canvas_pixel(VGA_WINDOW * wnd, int x, int y)
{
    return *(wnd->canvas.buffer + y * wnd->canvas.bound.width + x);
}

void set_canvas_pixel(VGA_WINDOW * wnd, int x, int y, int color)
{
    if(x < 0 || x > wnd->canvas.bound.width-1)
        return;
    if(y < 0 || y > wnd->canvas.bound.height-1)  
        return;
    *(wnd->canvas.buffer + y * wnd->canvas.bound.width + x) = color;
}

void bring_window_forward(int window_id)
{
    VGA_WINDOW * wnd = get_window(window_id);
	if(wnd == NULL)
		return;

    if(wnd == window_list_head)
        return;

    if(wnd == window_list_tail)
        window_list_tail = wnd->prev;

    wnd->prev->next = wnd->next;
    wnd->next->prev = wnd->prev;
    wnd->next = window_list_head;
    window_list_head->prev = wnd;
    wnd->prev = NULL;
    window_list_head = wnd;
}

/********************************************************************************
 *                             QUADTREE FUNCTIONS                               *
 * *****************************************************************************/

BOUND create_bound(int x, int y, int width, int height)
{
    BOUND bound;
    bound.x = x;
    bound.y = y;
    bound.width = width;
    bound.height = height;
    return bound;
}

int bound_contains(BOUND * b, int x, int y)
{
    if( (x >= b->x) && (x < b->x+b->width) ) {
        if( (y >= b->y) && (y < b->y+b->height) ) {
            return 1;
        }
    }
    return 0;
}

int bound_intersects(BOUND * a, BOUND * b)
{
    if(a->x >= (b->x+b->width) || b->x >= (a->x+a->width))
        return 0;

    if(a->y >= (b->y+b->height) || b->y >= (a->y+a->height))
        return 0;

    return 1;
}

int bound_contains_within(BOUND * in, BOUND * out)
{
    if( (in->x >= out->x) && (in->x+in->width <= out->x+out->width) ) {
        if( (in->y >= out->y) && (in->y+in->height <= out->y+out->height) )
        {
            return 1;
        }
    }
    return 0;
}

BOUND get_intersection(BOUND * a, BOUND * b)
{
    BOUND intr;

    // find x
    if(a->x >= b->x)
        intr.x = a->x;
    else
        intr.x = b->x;

    // find y
    if(a->y >= b->y)
        intr.y = a->y;
    else
        intr.y = b->y;

    // find width
    if((a->x+a->width) <= (b->x+b->width))
        intr.width = (a->x+a->width)-intr.x;
    else
        intr.width = (b->x+b->width)-intr.x;

    // find height
    if((a->y+a->height) <= (b->y+b->height))
        intr.height = (a->y+a->height)-intr.y;
    else
        intr.height = (b->y+b->height)-intr.y;

    // ret
    return intr;
}

QNODE * create_qnode(int x, int y, int width, int height)
{
    QNODE * q = malloc( sizeof(QNODE) );
    q->hidden = 0;
    q->children = 0;
    q->bound.x = x;
	q->bound.y = y;
	q->bound.width = width;
	q->bound.height = height;
    q->ne = NULL;
    q->nw = NULL;
    q->se = NULL;
    q->sw = NULL;
    return q;
}

void reset_root(QNODE * root)
{
    destroy_children(root);
    root->hidden = 0;
    root->children = 0;
}

QNODE * destroy_qnode(QNODE * q)
{
    if(q) {
        destroy_children(q);
        free(q);
        q = NULL;
    }
    return q;
}

void destroy_children(QNODE * q)
{
    if(q->children > 0) {
        if (q->nw)
            q->nw = destroy_qnode(q->nw);
        if (q->ne)
            q->ne = destroy_qnode(q->ne);
        if (q->se)
            q->se = destroy_qnode(q->se);
        if (q->sw)
            q->sw = destroy_qnode(q->sw);
        q->children = 0;
    }
}

int search_qtree(QNODE * q, int x, int y)
{
    if (q->hidden == 1) {
        return 1;
    }
    else {
        if (q->nw && bound_contains(&(q->nw->bound), x, y))
            return search_qtree(q->nw, x, y);
        if (q->ne && bound_contains(&(q->ne->bound), x, y))
            return search_qtree(q->ne, x, y);
        if (q->sw && bound_contains(&(q->sw->bound), x, y))
            return search_qtree(q->sw, x, y);
        if (q->se && bound_contains(&(q->se->bound), x, y))
            return search_qtree(q->se, x, y);
    }
    return 0;
}

void subdivide(QNODE * q, BOUND b)
{

    int x = q->bound.x;
    int y = q->bound.y;
    int w = q->bound.width;
    int h = q->bound.height;

    BOUND nw = create_bound(x,y,(w/2),(h/2));
    BOUND ne = create_bound(x+(w/2),y,(w/2),(h/2));
    BOUND sw = create_bound(x,y+(h/2),(w/2),(h/2));
    BOUND se = create_bound(x+(w/2),y+(h/2),(w/2),(h/2));

    
    if (bound_intersects(&(nw),&b)) {
        if(!q->nw) {
            q->children++;
            q->nw = create_qnode(nw.x,nw.y,nw.width,nw.height);
        }
        check_qnode(q->nw, b);
    }

    if (bound_intersects(&(ne),&b)) {
        if(!q->ne) {
            q->children++;
            q->ne = create_qnode(ne.x,ne.y,ne.width,ne.height);
        }
        check_qnode(q->ne, b);
    }

    if (bound_intersects(&(sw),&b)) {
        if(!q->sw) {
            q->children++;
            q->sw = create_qnode(sw.x,sw.y,sw.width,sw.height);
        }
        check_qnode(q->sw, b);
    }

    if (bound_intersects(&(se),&b)) {
        if(!q->se) {
            q->children++;
            q->se = create_qnode(se.x,se.y,se.width,se.height);
        }
        check_qnode(q->se, b);
    }
}

void check_qnode(QNODE * q, BOUND b)
{
    if(bound_contains_within(&(q->bound), &(b))) {
        q->hidden = 1;
    }
    else if(bound_intersects(&(q->bound), &b)) {
        subdivide(q, b);
    }
}

void reset_qtrees()
{
    VGA_WINDOW * w_ptr = window_list_head;

    while(w_ptr != NULL){
        reset_root(w_ptr->root);
        w_ptr = w_ptr->next;
    }
}

void build_quadtrees()
{
    if(!window_list_tail)
        return;

    reset_qtrees();

    VGA_WINDOW * w_ptr = window_list_tail;
    VGA_WINDOW * f_ptr;

    while(w_ptr != NULL) {

        f_ptr = w_ptr->prev;
        while(f_ptr != NULL) {
            check_qnode(w_ptr->root, 
                get_intersection(
                    &(w_ptr->frame.bound), 
                    &(f_ptr->frame.bound)));
            f_ptr = f_ptr->prev;
        }
        w_ptr = w_ptr->prev;
    }
}


/********************************************************************************
 *                                TEST PROCESS                                  *
 * *****************************************************************************/

void vga_test (PROCESS proc, PARAM param)
{
	VGA_WINDOW_MSG msg;

	// Create Window 1
	msg.cmd = VGA_CREATE_WINDOW;
	msg.u.create_window.title = "Window 1";
	msg.u.create_window.x = 50;
	msg.u.create_window.y = 50;
	msg.u.create_window.width = 100;
	msg.u.create_window.height = 50;
	send(vga_port, &msg);
	unsigned int window1_id = msg.u.create_window.window_id;

	// Create Window 2
	msg.cmd = VGA_CREATE_WINDOW;
	msg.u.create_window.title = "Window 2";
	msg.u.create_window.x = 10;
	msg.u.create_window.y = 120;
	msg.u.create_window.width = 150;
	msg.u.create_window.height = 60;
	send(vga_port, &msg);
	unsigned int window2_id = msg.u.create_window.window_id;

	// Create Window 3
	msg.cmd = VGA_CREATE_WINDOW;
	msg.u.create_window.title = "Window 3";
	msg.u.create_window.x = 100;
	msg.u.create_window.y = 30;
	msg.u.create_window.width = 100;
	msg.u.create_window.height = 100;
	send(vga_port, &msg);
	unsigned int window3_id = msg.u.create_window.window_id;

    // Create Window 4
	msg.cmd = VGA_CREATE_WINDOW;
	msg.u.create_window.title = "Window 4";
	msg.u.create_window.x = 120;
	msg.u.create_window.y = 70;
	msg.u.create_window.width = 100;
	msg.u.create_window.height = 100;
	send(vga_port, &msg);
	unsigned int window4_id = msg.u.create_window.window_id;

	// Draw some lines in Window 1
	char current_color = 0;
	for (int x = 0; x < 100; x += 2) {
		msg.cmd = VGA_DRAW_LINE;
		msg.u.draw_line.window_id = window1_id;
		msg.u.draw_line.x0 = x;
		msg.u.draw_line.y0 = 0;
		msg.u.draw_line.x1 = 100 - x;
		msg.u.draw_line.y1 = 49;
		msg.u.draw_line.color = current_color++;
		send(vga_port, &msg);
	}

	// Write some text in Window 2
	msg.cmd = VGA_DRAW_TEXT;
	msg.u.draw_text.window_id = window2_id;
	msg.u.draw_text.text = "Hello CSC720!";
	msg.u.draw_text.x = 1;
	msg.u.draw_text.y = 1;
	msg.u.draw_text.fg_color = 0x3f; // White
	msg.u.draw_text.bg_color = 0;
	send(vga_port, &msg);

	// Write some text in Window 2 that will be clipped
	msg.cmd = VGA_DRAW_TEXT;
	msg.u.draw_text.window_id = window2_id;
	msg.u.draw_text.text = "Text that is too long will be clipped";
	msg.u.draw_text.x = 20;
	msg.u.draw_text.y = 20;
	msg.u.draw_text.fg_color = 0x3f; // White
	msg.u.draw_text.bg_color = 0;
	send(vga_port, &msg);
/*
	// Draw some random pixels in Window 3
	msg.cmd = VGA_DRAW_PIXEL;
	msg.u.draw_pixel.window_id = window3_id;
	current_color = 0;
	for (int x = 3; x < 100; x += 5) {
		for (int y = 3; y < 100; y += 5) {
		msg.u.draw_pixel.x = x;
		msg.u.draw_pixel.y = y;
		msg.u.draw_pixel.color = current_color;
		current_color = (current_color + 1) % 64;
		send(vga_port, &msg);
		}
	}
*/
    msg.cmd = VGA_CHANGE_FOCUS;
    msg.u.change_focus.window_id = window1_id;
    send(vga_port, &msg);
    msg.u.change_focus.window_id = window2_id;
    send(vga_port, &msg);
    msg.u.change_focus.window_id = window3_id;
    send(vga_port, &msg);

    become_zombie();
}
