#include "../WtDtCore/ParserAdapter.h"
#include "../WtDtCore/DataManager.h"
#include "../WtDtCore/StateMonitor.h"
#include "../WtDtCore/UDPCaster.h"

#include "../WTSUtils/pugixml/pugixml.hpp"

#include "../Share/WTSSessionInfo.hpp"
#include "../Share/DLLHelper.hpp"
#include "../Share/JsonToVariant.hpp"
#include "../Share/WTSVariant.hpp"

#include "../WTSTools/WTSHotMgr.h"
#include "../WTSTools/WTSParamParser.h"
#include "../WTSTools/WTSBaseDataMgr.h"
#include "../WTSTools/WTSLogger.h"

#include <boost/asio.hpp>

WTSBaseDataMgr	g_baseDataMgr;
WTSHotMgr		g_hotMgr;
boost::asio::io_service g_asyncIO;
StateMonitor	g_stateMon;
UDPCaster		g_udpCaster;
DataManager		g_dataMgr;

#ifdef _WIN32
#include "../Common/mdump.h"
DWORD g_dwMainThreadId = 0;
BOOL WINAPI ConsoleCtrlhandler(DWORD dwCtrlType)
{
	switch (dwCtrlType)
	{
	case CTRL_CLOSE_EVENT:
	{
		g_dataMgr.release();

		PostThreadMessage(g_dwMainThreadId, WM_QUIT, 0, 0);
	}
	break;
	}

	return TRUE;
}
#endif


void initDataMgr(WTSVariant* config)
{
	g_dataMgr.init(config, &g_baseDataMgr, &g_stateMon, &g_udpCaster);
}

void initParsers(WTSVariant* cfg)
{
	for (uint32_t idx = 0; idx < cfg->size(); idx++)
	{
		WTSVariant* cfgItem = cfg->get(idx);
		if (!cfgItem->getBoolean("active"))
			continue;

		const char* path = cfgItem->getCString("module");
		DllHandle libParser = DLLHelper::load_library(path);
		if (libParser)
		{
			FuncCreateParser pFuncCreateParser = (FuncCreateParser)DLLHelper::get_symbol(libParser, "createParser");
			if (pFuncCreateParser == NULL)
			{
				WTSLogger::info("����ģ���ʼ��ʧ��,�Ҳ�����Ӧ����ں���...");
			}

			FuncDeleteParser pFuncDeleteParser = (FuncDeleteParser)DLLHelper::get_symbol(libParser, "deleteParser");
			if (pFuncDeleteParser == NULL)
			{
				WTSLogger::info("����ģ���ʼ��ʧ��,�Ҳ�����Ӧ����ں���...");
			}

			if (pFuncCreateParser && pFuncDeleteParser)
			{
				WTSParams* params = cfgItem->toParams();

				ParserAdapterPtr adapter(new ParserAdapter(&g_baseDataMgr, &g_dataMgr));
				adapter->initAdapter(params, pFuncCreateParser, pFuncDeleteParser);
				ParserAdapterMgr::addAdapter(adapter);
				params->release();
			}

		}
		else
		{
			WTSLogger::info("����ģ���ʼ��ʧ��,����ģ��%sʧ��...", path);
		}
	}

	WTSLogger::info("һ������%u��Parser", ParserAdapterMgr::size());
}

void initialize()
{
	std::string json;
	StdFile::read_file_content("QFConfig.json", json);
	rj::Document document;
	document.Parse(json.c_str());

	WTSVariant* config = WTSVariant::createObject();
	jsonToVariant(document, config);

	const char* id = config->getCString("id");
	if (strlen(id) > 0)
	{
#ifdef _WIN32
		HANDLE hHandle = ::CreateEvent(NULL, TRUE, TRUE, id);
		DWORD dwErr = GetLastError();
		if (hHandle != NULL && dwErr == ERROR_ALREADY_EXISTS)
		{
			ExitProcess(0);
			return;
		}
#else
		WTSLogger::error("Single instance mode of QuoteFactory is not OK on linux yet.");
#endif
	}


	//�����г���Ϣ
	WTSVariant* cfgBF = config->get("basefiles");
	if (cfgBF->get("session"))
	{
		g_baseDataMgr.loadSessions(cfgBF->getCString("session"));
		WTSLogger::info("����ʱ��ģ��������");
	}

	if (cfgBF->get("commodity"))
	{
		g_baseDataMgr.loadCommodities(cfgBF->getCString("commodity"));
		WTSLogger::info("Ʒ���б��������");
	}

	if (cfgBF->get("contract"))
	{
		g_baseDataMgr.loadContracts(cfgBF->getCString("contract"));
		WTSLogger::info("��Լ�б��������");
	}

	if (cfgBF->get("holiday"))
	{
		g_baseDataMgr.loadHolidays(cfgBF->getCString("holiday"));
		WTSLogger::info("�ڼ���ģ��������");
	}

	if (cfgBF->get("hot"))
	{
		g_hotMgr.loadHots(cfgBF->getCString("hot"));
		WTSLogger::info("�����л����������");
	}

	g_udpCaster.init(config->get("broadcaster"), &g_baseDataMgr, &g_dataMgr);

	initDataMgr(config->get("writer"));

	g_stateMon.initialize("statemonitor.json", &g_baseDataMgr, &g_dataMgr);

	initParsers(config->get("parsers"));

	config->release();

	g_asyncIO.post([](){
		boost::this_thread::sleep(boost::posix_time::millisec(5));
		g_stateMon.run();
	});
}

int main()
{
	WTSLogger::init();

#ifdef _WIN32
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);

	_set_error_mode(_OUT_TO_STDERR);
	_set_abort_behavior(0, _WRITE_ABORT_MSG);

	g_dwMainThreadId = GetCurrentThreadId();
	SetConsoleCtrlHandler(ConsoleCtrlhandler, TRUE);

	CMiniDumper::Enable("QuoteFactory.exe", true);
#endif

	initialize();

	boost::asio::io_service::work work(g_asyncIO);
	g_asyncIO.run();
}
