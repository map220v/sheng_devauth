LIBS=-Llibs -lrpmbservice -lminkadaptor -lqcomtee -lqcbor -Iminkipc -Iidl

.PHONY: idl

xiaomi_devauth: main.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

idl:
	idl/idlc idl/IAppController.idl -o idl/IAppController.h
	idl/idlc idl/IAppLoader.idl -o idl/IAppLoader.h
	idl/idlc idl/IClientEnv.idl -o idl/IClientEnv.h
	idl/idlc idl/ICredentials.idl -o idl/ICredentials.h
	idl/idlc idl/IIO.idl -o idl/IIO.h
	idl/idlc idl/IQSEEComCompat.idl -o idl/IQSEEComCompat.h
	idl/idlc idl/IQSEEComCompatAppLoader.idl -o idl/IQSEEComCompatAppLoader.h
#	idl/idlc --skel idl/IClientEnv.idl -o idl/IClientEnv_invoke.h
