/*
 * (C) 2011 by Pablo Neira Ayuso <pablo@netfilter.org>
 * (C) 2011 by Intra2net AG <http://www.intra2net.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#include <libmnl/libmnl.h>
#include <libnetfilter_acct/libnetfilter_acct.h>

enum {
	NFACCT_CMD_NONE = 0,
	NFACCT_CMD_LIST,
	NFACCT_CMD_ADD,
	NFACCT_CMD_DELETE,
	NFACCT_CMD_GET,
	NFACCT_CMD_FLUSH,
	NFACCT_CMD_VERSION,
	NFACCT_CMD_HELP,
};

static int nfacct_cmd_list(int argc, char *argv[]);
static int nfacct_cmd_add(int argc, char *argv[]);
static int nfacct_cmd_delete(int argc, char *argv[]);
static int nfacct_cmd_get(int argc, char *argv[]);
static int nfacct_cmd_flush(int argc, char *argv[]);
static int nfacct_cmd_version(int argc, char *argv[]);
static int nfacct_cmd_help(int argc, char *argv[]);

static void usage(char *argv[])
{
	fprintf(stderr, "Usage: %s command [parameters]...\n", argv[0]);
}

static void nfacct_perror(const char *msg)
{
	if (errno == 0) {
		fprintf(stderr, "nfacct v%s: %s\n", VERSION, msg);
	} else {
		fprintf(stderr, "nfacct v%s: %s: %s\n",
			VERSION, msg, strerror(errno));
	}
}

int main(int argc, char *argv[])
{
	int cmd = NFACCT_CMD_NONE, ret = 0;

	if (argc < 2) {
		usage(argv);
		exit(EXIT_FAILURE);
	}

	if (strncmp(argv[1], "list", strlen(argv[1])) == 0)
		cmd = NFACCT_CMD_LIST;
	else if (strncmp(argv[1], "add", strlen(argv[1])) == 0)
		cmd = NFACCT_CMD_ADD;
	else if (strncmp(argv[1], "delete", strlen(argv[1])) == 0)
		cmd = NFACCT_CMD_DELETE;
	else if (strncmp(argv[1], "get", strlen(argv[1])) == 0)
		cmd = NFACCT_CMD_GET;
	else if (strncmp(argv[1], "flush", strlen(argv[1])) == 0)
		cmd = NFACCT_CMD_FLUSH;
	else if (strncmp(argv[1], "version", strlen(argv[1])) == 0)
		cmd = NFACCT_CMD_VERSION;
	else if (strncmp(argv[1], "help", strlen(argv[1])) == 0)
		cmd = NFACCT_CMD_HELP;
	else {
		fprintf(stderr, "nfacct v%s: Unknown command: %s\n",
			VERSION, argv[1]);
		usage(argv);
		exit(EXIT_FAILURE);
	}

	switch(cmd) {
	case NFACCT_CMD_LIST:
		ret = nfacct_cmd_list(argc, argv);
		break;
	case NFACCT_CMD_ADD:
		ret = nfacct_cmd_add(argc, argv);
		break;
	case NFACCT_CMD_DELETE:
		ret = nfacct_cmd_delete(argc, argv);
		break;
	case NFACCT_CMD_GET:
		ret = nfacct_cmd_get(argc, argv);
		break;
	case NFACCT_CMD_FLUSH:
		ret = nfacct_cmd_flush(argc, argv);
		break;
	case NFACCT_CMD_VERSION:
		ret = nfacct_cmd_version(argc, argv);
		break;
	case NFACCT_CMD_HELP:
		ret = nfacct_cmd_help(argc, argv);
		break;
	}
	return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int nfacct_cb(const struct nlmsghdr *nlh, void *data)
{
	struct nfacct *nfacct;
	char buf[4096];

	nfacct = nfacct_alloc();
	if (nfacct == NULL) {
		nfacct_perror("OOM");
		goto err;
	}

	if (nfacct_nlmsg_parse_payload(nlh, nfacct) < 0) {
		nfacct_perror("nfacct_parse_nl_msg");
		goto err_free;
	}

	nfacct_snprintf(buf, sizeof(buf), nfacct, NFACCT_SNPRINTF_F_FULL);
	printf("%s\n", buf);

err_free:
	nfacct_free(nfacct);
err:
	return MNL_CB_OK;
}

static int nfacct_cmd_list(int argc, char *argv[])
{
	bool zeroctr = false;
	struct mnl_socket *nl;
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	unsigned int seq, portid;
	int ret;

	if (argc == 3) {
		if (strncmp(argv[2], "reset", strlen(argv[2])) == 0) {
			zeroctr = true;
		}
	}

	seq = time(NULL);
	nlh = nfacct_nlmsg_build_hdr(buf, zeroctr ?
					NFNL_MSG_ACCT_GET_CTRZERO :
					NFNL_MSG_ACCT_GET,
				     NLM_F_DUMP, seq);

	nl = mnl_socket_open(NETLINK_NETFILTER);
	if (nl == NULL) {
		nfacct_perror("mnl_socket_open");
		return -1;
	}

	if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
		nfacct_perror("mnl_socket_bind");
		return -1;
	}
	portid = mnl_socket_get_portid(nl);

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_send");
		exit(EXIT_FAILURE);
	}

	ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	while (ret > 0) {
		ret = mnl_cb_run(buf, ret, seq, portid, nfacct_cb, NULL);
		if (ret <= 0)
			break;
		ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	}
	if (ret == -1) {
		nfacct_perror("error");
		return -1;
	}
	mnl_socket_close(nl);

	return 0;
}

static int nfacct_cmd_add(int argc, char *argv[])
{
	struct mnl_socket *nl;
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	uint32_t portid, seq;
	struct nfacct *nfacct;
	int ret;

	if (argc < 3) {
		nfacct_perror("missing object name");
		exit(EXIT_FAILURE);
	}

	nfacct = nfacct_alloc();
	if (nfacct == NULL) {
		nfacct_perror("OOM");
		exit(EXIT_FAILURE);
	}

	nfacct_attr_set(nfacct, NFACCT_ATTR_NAME, argv[2]);

	seq = time(NULL);
	nlh = nfacct_nlmsg_build_hdr(buf, NFNL_MSG_ACCT_NEW,
				     NLM_F_CREATE | NLM_F_ACK, seq);
	nfacct_nlmsg_build_payload(nlh, nfacct);

	nfacct_free(nfacct);

	nl = mnl_socket_open(NETLINK_NETFILTER);
	if (nl == NULL) {
		nfacct_perror("mnl_socket_open");
		return -1;
	}

	if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
		nfacct_perror("mnl_socket_bind");
		return -1;
	}
	portid = mnl_socket_get_portid(nl);

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		nfacct_perror("mnl_socket_send");
		return -1;
	}

	ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	while (ret > 0) {
		ret = mnl_cb_run(buf, ret, seq, portid, NULL, NULL);
		if (ret <= 0)
			break;
		ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	}
	if (ret == -1) {
		nfacct_perror("error");
		return -1;
	}
	mnl_socket_close(nl);

	return 0;
}

static int nfacct_cmd_delete(int argc, char *argv[])
{
	struct mnl_socket *nl;
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	uint32_t portid, seq;
	struct nfacct *nfacct;
	int ret;

	if (argc < 3) {
		nfacct_perror("missing object name");
		return -1;
	}

	nfacct = nfacct_alloc();
	if (nfacct == NULL) {
		nfacct_perror("OOM");
		return -1;
	}

	nfacct_attr_set(nfacct, NFACCT_ATTR_NAME, argv[2]);

	seq = time(NULL);
	nlh = nfacct_nlmsg_build_hdr(buf, NFNL_MSG_ACCT_DEL,
				     NLM_F_ACK, seq);
	nfacct_nlmsg_build_payload(nlh, nfacct);

	nfacct_free(nfacct);

	nl = mnl_socket_open(NETLINK_NETFILTER);
	if (nl == NULL) {
		nfacct_perror("mnl_socket_open");
		return -1;
	}

	if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
		nfacct_perror("mnl_socket_bind");
		return -1;
	}
	portid = mnl_socket_get_portid(nl);

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		nfacct_perror("mnl_socket_send");
		return -1;
	}

	ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	while (ret > 0) {
		ret = mnl_cb_run(buf, ret, seq, portid, NULL, NULL);
		if (ret <= 0)
			break;
		ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	}
	if (ret == -1) {
		nfacct_perror("error");
		return -1;
	}

	mnl_socket_close(nl);

	return 0;
}

static int nfacct_cmd_get(int argc, char *argv[])
{
	bool zeroctr = false;
	struct mnl_socket *nl;
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	uint32_t portid, seq;
	struct nfacct *nfacct;
	int ret;

	if (argc < 3) {
		nfacct_perror("missing object name");
		exit(EXIT_FAILURE);
	}

	if (argc == 4) {
		if (strncmp(argv[3], "reset", strlen(argv[3])) == 0) {
			zeroctr = true;
		}
	}

	nfacct = nfacct_alloc();
	if (nfacct == NULL) {
		nfacct_perror("OOM");
		exit(EXIT_FAILURE);
	}
	nfacct_attr_set(nfacct, NFACCT_ATTR_NAME, argv[2]);

	seq = time(NULL);
	nlh = nfacct_nlmsg_build_hdr(buf, zeroctr ?
					NFNL_MSG_ACCT_GET_CTRZERO :
					NFNL_MSG_ACCT_GET,
				     NLM_F_ACK, seq);

	nfacct_nlmsg_build_payload(nlh, nfacct);

	nfacct_free(nfacct);

	nl = mnl_socket_open(NETLINK_NETFILTER);
	if (nl == NULL) {
		nfacct_perror("mnl_socket_open");
		return -1;
	}

	if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
		nfacct_perror("mnl_socket_bind");
		return -1;
	}
	portid = mnl_socket_get_portid(nl);

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		nfacct_perror("mnl_socket_send");
		return -1;
	}

	ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	while (ret > 0) {
		ret = mnl_cb_run(buf, ret, seq, portid, nfacct_cb, NULL);
		if (ret <= 0)
			break;
		ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	}
	if (ret == -1) {
		nfacct_perror("error");
		return -1;
	}
	mnl_socket_close(nl);

	return 0;
}

static int nfacct_cmd_flush(int argc, char *argv[])
{
	struct mnl_socket *nl;
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	uint32_t portid, seq;
	int ret;

	seq = time(NULL);
	nlh = nfacct_nlmsg_build_hdr(buf, NFNL_MSG_ACCT_DEL, NLM_F_ACK, seq);

	nl = mnl_socket_open(NETLINK_NETFILTER);
	if (nl == NULL) {
		nfacct_perror("mnl_socket_open");
		return -1;
	}

	if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
		nfacct_perror("mnl_socket_bind");
		return -1;
	}
	portid = mnl_socket_get_portid(nl);

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		nfacct_perror("mnl_socket_send");
		return -1;
	}

	ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	while (ret > 0) {
		ret = mnl_cb_run(buf, ret, seq, portid, NULL, NULL);
		if (ret <= 0)
			break;
		ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	}
	if (ret == -1) {
		nfacct_perror("error");
		return -1;
	}

	mnl_socket_close(nl);

	return 0;
}

static const char version_msg[] =
	"nfacct v%s: utility for the Netfilter extended accounting "
	"infrastructure\n"
	"Copyright (C) 2011 Pablo Neira Ayuso <pablo@netfilter.org>\n"
	"Copyright (C) 2011 Intra2net AG <http://www.intra2net.com>\n"
	"This program comes with ABSOLUTELY NO WARRANTY.\n"
	"This is free software, and you are welcome to redistribute it under "
	"certain \nconditions; see LICENSE file distributed in this package "
	"for details.\n";

static int nfacct_cmd_version(int argc, char *argv[])
{
	printf(version_msg, VERSION);
	return 0;
}

static const char help_msg[] =
	"nfacct v%s: utility for the Netfilter extended accounting "
	"infrastructure\n"
	"Usage: %s command [parameters]...\n\n"
	"Commands:\n"
	"  list [reset]\t\tList the accounting object table (and reset)\n"
	"  add object-name\tAdd new accounting object to table\n"
	"  delete object-name\tDelete existing accounting object\n"
	"  get object-name\tGet existing accounting object\n"
	"  flush\t\t\tFlush accounting object table\n"
	"  version\t\tDisplay version and disclaimer\n"
	"  help\t\t\tDisplay this help message\n";

static int nfacct_cmd_help(int argc, char *argv[])
{
	printf(help_msg, VERSION, argv[0]);
	return 0;
}
