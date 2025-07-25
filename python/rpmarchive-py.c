#include "rpmsystem-py.h"

#include <rpm/rpmtypes.h>
#include <rpm/rpmfi.h>

#include "header-py.h"
#include "rpmfd-py.h"
#include "rpmfiles-py.h"
#include "rpmarchive-py.h"
#include "rpmstrpool-py.h"

struct rpmarchiveObject_s {
    PyObject_HEAD
    rpmfi archive;
    rpmfiles files;
};

static void rpmarchive_dealloc(rpmarchiveObject * s)
{
    PyObject_GC_UnTrack(s);
    rpmfilesFree(s->files);
    rpmfiArchiveClose(s->archive);
    rpmfiFree(s->archive);
    PyTypeObject *type = Py_TYPE(s);
    freefunc free = PyType_GetSlot(type, Py_tp_free);
    free(s);
    Py_DECREF(type);
}

static int rpmarchive_traverse(hdrObject * s, visitproc visit, void *arg)
{
    if (python_version >= 0x03090000) {
        Py_VISIT(Py_TYPE(s));
    }
    return 0;
}

static PyObject *rpmarchive_error(int rc)
{
    PyErr_SetObject(PyExc_IOError,
		    Py_BuildValue("(is)", rc, rpmfileStrerror(rc)));
    return NULL;
}

static PyObject *rpmarchive_closed(void)
{
    PyErr_SetString(PyExc_IOError, "I/O operation on closed archive");
    return NULL;
}

static PyObject *rpmarchive_tell(rpmarchiveObject *s)
{
    return PyLong_FromLongLong(rpmfiArchiveTell(s->archive));
}

static PyObject *rpmarchive_close(rpmarchiveObject *s)
{
    if (s->archive) {
	int rc = rpmfiArchiveClose(s->archive);
	s->archive = rpmfiFree(s->archive);
	if (rc)
	    return rpmarchive_error(rc);
    }
    Py_RETURN_NONE;
}

static PyObject *rpmarchive_has_content(rpmarchiveObject *s)
{
    return PyLong_FromLong(rpmfiArchiveHasContent(s->archive));
}

static PyObject *rpmarchive_read(rpmarchiveObject *s,
				 PyObject *args, PyObject *kwds)
{
    char *kwlist[] = { "size", NULL };
    char buf[BUFSIZ];
    ssize_t chunksize = sizeof(buf);
    ssize_t left = -1;
    ssize_t nb = 0;
    PyObject *res = NULL;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|l", kwlist, &left))
	return NULL;

    if (s->archive == NULL)
	return rpmarchive_closed();

    /* ConcatAndDel() doesn't work on NULL string, meh */
    res = PyBytes_FromStringAndSize(NULL, 0);
    do {
	if (left >= 0 && left < chunksize)
	    chunksize = left;

	Py_BEGIN_ALLOW_THREADS 
	nb = rpmfiArchiveRead(s->archive, buf, chunksize);
	Py_END_ALLOW_THREADS 

	if (nb > 0) {
	    PyObject *tmp = PyBytes_FromStringAndSize(buf, nb);
	    PyBytes_ConcatAndDel(&res, tmp);
	    left -= nb;
	}
    } while (nb > 0);

    if (nb < 0) {
	Py_XDECREF(res);
	return rpmarchive_error(nb);
    } else {
	return res;
    }
}

static PyObject *rpmarchive_write(rpmarchiveObject *s,
				 PyObject *args, PyObject *kwds)
{
    const char *buf = NULL;
    ssize_t size = 0;
    char *kwlist[] = { "buffer", NULL };
    ssize_t rc = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#", kwlist, &buf, &size)) {
	return NULL;
    }

    if (s->archive == NULL)
	return rpmarchive_closed();

    Py_BEGIN_ALLOW_THREADS 
    rc = rpmfiArchiveWrite(s->archive, buf, size);
    Py_END_ALLOW_THREADS 

    if (rc < 0)
	return rpmarchive_error(rc);
    else
	return Py_BuildValue("n", rc);
}

static PyObject *rpmarchive_readto(rpmarchiveObject *s,
				 PyObject *args, PyObject *kwds)
{
    rpmfdObject *fdo = NULL;
    int nodigest = 0;
    int rc;
    char *kwlist[] = { "fd", "nodigest", NULL };
    PyObject *fdo_source;
    rpmmodule_state_t *modstate = rpmModState_FromObject((PyObject*)s);
    if (!modstate) {
        return NULL;
    }

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|i", kwlist,
				 &fdo_source, &nodigest)) {
	return NULL;
    }
    if(!rpmfdFromPyObject(modstate, fdo_source, &fdo)) {
        return NULL;
    }

    if (s->archive == NULL) {
	Py_DECREF(fdo);
	return rpmarchive_closed();
    }


    Py_BEGIN_ALLOW_THREADS
    rc = rpmfiArchiveReadToFile(s->archive, rpmfdGetFd(fdo), nodigest);
    Py_END_ALLOW_THREADS
    Py_DECREF(fdo);

    if (rc)
	return rpmarchive_error(rc);
    
    Py_RETURN_NONE;
}

static PyObject *rpmarchive_writeto(rpmarchiveObject *s,
				 PyObject *args, PyObject *kwds)
{
    rpmfdObject *fdo = NULL;
    int rc;
    char *kwlist[] = { "fd", NULL };
    PyObject *fdo_source;
    rpmmodule_state_t *modstate = rpmModState_FromObject((PyObject*)s);
    if (!modstate) {
        return NULL;
    }

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O", kwlist,
				 &fdo_source)) {
	return NULL;
    }
    if(!rpmfdFromPyObject(modstate, fdo_source, &fdo)) {
        return NULL;
    }

    if (s->archive == NULL) {
	Py_DECREF(fdo);
	return rpmarchive_closed();
    }

    Py_BEGIN_ALLOW_THREADS
    rc = rpmfiArchiveWriteFile(s->archive, rpmfdGetFd(fdo));
    Py_END_ALLOW_THREADS
    Py_DECREF(fdo);

    if (rc)
	return rpmarchive_error(rc);

    Py_RETURN_NONE;
}

static struct PyMethodDef rpmarchive_methods[] = {
    { "tell",	(PyCFunction)rpmarchive_tell,		METH_NOARGS,
      "archive.tell() -- Return current position in archive." },
    { "close",	(PyCFunction)rpmarchive_close,		METH_NOARGS,
      "archive.close() -- Close archive and do final consistency checks."},
    { "read",	(PyCFunction)rpmarchive_read,	METH_VARARGS|METH_KEYWORDS,
      "archive.read(size=None) -- Read next size bytes from current file.\n\n"
      "Returns bytes\n"},
    { "write",	(PyCFunction)rpmarchive_write,	METH_VARARGS|METH_KEYWORDS,
      "archive.write(buffer) -- Write buffer to current file." },
    { "readto",	(PyCFunction)rpmarchive_readto,	METH_VARARGS|METH_KEYWORDS,
      "archive.readto(fd, nodigest=None) -- Read content of fd\n"
      "and write as content of the current file to archive." },
    { "writeto", (PyCFunction)rpmarchive_writeto,METH_VARARGS|METH_KEYWORDS,
      "archive.writeto(fd) -- Write content of current file in archive\n to fd." },
    { "hascontent", (PyCFunction)rpmarchive_has_content, METH_NOARGS,
      "archive.hascontent() -- Return if current file has a content.\n\n"
      "Returns false for non regular and all but one of hardlinked files."},
    { NULL, NULL, 0, NULL }
};

static char rpmarchive_doc[] =
"Gives access to the payload of an rpm package.\n\n"
"Is returned by .archive() method of an rpm.files instance.\n"
"All methods can raise an IOError exception.";

static PyObject *rpmarchive_iternext(rpmarchiveObject *s)
{
    PyObject *next = NULL;
    rpmmodule_state_t *modstate = rpmModState_FromObject((PyObject*)s);
    if (!modstate) {
	    return NULL;
    }

    int fx = rpmfiNext(s->archive);

    if (fx >= 0) {
	next = rpmfile_Wrap(modstate, s->files, fx);
    } else if (fx < -1) {
	next = rpmarchive_error(fx);
    } else {
	/* end of iteration, nothing to do */
    }
    
    return next;
}

static PyObject *disabled_new(PyTypeObject *type,
                              PyObject *args, PyObject *kwds)
{
    PyErr_SetString(PyExc_TypeError,
                    "TypeError: cannot create 'rpm.archive' instances");
    return NULL;
}

static PyType_Slot rpmarchive_Type_Slots[] = {
    {Py_tp_new, disabled_new},
    {Py_tp_dealloc, rpmarchive_dealloc},
    {Py_tp_traverse, rpmarchive_traverse},
    {Py_tp_getattro, PyObject_GenericGetAttr},
    {Py_tp_setattro, PyObject_GenericSetAttr},
    {Py_tp_doc, rpmarchive_doc},
    {Py_tp_iter, PyObject_SelfIter},
    {Py_tp_iternext, rpmarchive_iternext},
    {Py_tp_methods, rpmarchive_methods},
    {0, NULL},
};
PyType_Spec rpmarchive_Type_Spec = {
    .name = "rpm.archive",
    .basicsize = sizeof(rpmarchiveObject),
    .flags = Py_TPFLAGS_DEFAULT| Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_IMMUTABLETYPE,
    .slots = rpmarchive_Type_Slots,
};

PyObject * rpmarchive_Wrap(PyTypeObject *subtype,
			   rpmfiles files, rpmfi archive)
{
    allocfunc subtype_alloc = (allocfunc)PyType_GetSlot(subtype, Py_tp_alloc);
    rpmarchiveObject *s = (rpmarchiveObject *)subtype_alloc(subtype, 0);
    if (s == NULL) return NULL;

    s->files = rpmfilesLink(files);
    s->archive = archive;

    return (PyObject *) s;
}

