/*
 * DemoFeaturePlugin.cpp - implementation of DemoFeaturePlugin class
 *
 * Copyright (c) 2017 Tobias Doerffel <tobydox/at/users/dot/sf/dot/net>
 *
 * This file is part of Veyon - http://veyon.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include "AuthenticationCredentials.h"
#include "Computer.h"
#include "CryptoCore.h"
#include "DemoClient.h"
#include "DemoFeaturePlugin.h"
#include "DemoServer.h"
#include "FeatureWorkerManager.h"
#include "VeyonConfiguration.h"
#include "Logger.h"


DemoFeaturePlugin::DemoFeaturePlugin() :
	m_fullscreenDemoFeature( Feature::Mode | Feature::AllComponents,
							 Feature::Uid( "7b6231bd-eb89-45d3-af32-f70663b2f878" ),
							 tr( "Fullscreen demo" ), tr( "Stop demo" ),
							 tr( "In this mode your screen is being displayed on "
								 "all computers. Furthermore the users "
								 "aren't able to do something else as all input "
								 "devices are locked in this mode." ),
							 ":/demo/presentation-fullscreen.png" ),
	m_windowDemoFeature( Feature::Mode | Feature::AllComponents,
						 Feature::Uid( "ae45c3db-dc2e-4204-ae8b-374cdab8c62c" ),
						 tr( "Window demo" ), tr( "Stop demo" ),
						 tr( "In this mode your screen being displayed in a "
							 "window on all computers. The users are "
							 "able to switch to other windows and thus "
							 "can continue to work." ),
						 ":/demo/presentation-window.png" ),
	m_demoServerFeature( Feature::Session | Feature::Service | Feature::Worker | Feature::Builtin,
						 Feature::Uid( "e4b6e743-1f5b-491d-9364-e091086200f4" ),
						 QString(), QString(), QString() ),
	m_demoClientFeature( Feature::Session | Feature::Service | Feature::Worker | Feature::Builtin,
						 Feature::Uid( "7b68b525-1114-4aea-8d42-ab4f26bbf5e5" ),
						 QString(), QString(), QString() ),
	m_features(),
	m_demoAccessToken( CryptoCore::generateChallenge().toBase64() ),
	m_demoServerToken( CryptoCore::generateChallenge().toBase64() ),
	m_demoClientHosts(),
	m_demoServer( nullptr ),
	m_demoClient( nullptr )
{
	m_features += m_fullscreenDemoFeature;
	m_features += m_windowDemoFeature;
	m_features += m_demoServerFeature;
	m_features += m_demoClientFeature;
}



DemoFeaturePlugin::~DemoFeaturePlugin()
{
}



bool DemoFeaturePlugin::startMasterFeature( const Feature& feature,
											const ComputerControlInterfaceList& computerControlInterfaces,
											ComputerControlInterface& localServiceInterface,
											QWidget* parent )
{
	if( feature == m_windowDemoFeature || feature == m_fullscreenDemoFeature )
	{
		localServiceInterface.sendFeatureMessage( FeatureMessage( m_demoServerFeature.uid(), StartDemoServer ).
												  addArgument( VncServerToken, m_demoServerToken ).
												  addArgument( DemoAccessToken, m_demoAccessToken ) );

		for( auto computerControlInterface : computerControlInterfaces )
		{
			m_demoClientHosts += computerControlInterface->computer().hostAddress();
		}

		qDebug() << "DemoFeaturePlugin::startMasterFeature(): clients:" << m_demoClientHosts;

		return sendFeatureMessage( FeatureMessage( m_demoClientFeature.uid(), StartDemoClient ).
								   addArgument( DemoAccessToken, m_demoAccessToken ).
								   addArgument( IsFullscreenDemo, feature == m_fullscreenDemoFeature ),
								   computerControlInterfaces );
	}

	return false;
}



bool DemoFeaturePlugin::stopMasterFeature( const Feature& feature,
										   const ComputerControlInterfaceList& computerControlInterfaces,
										   ComputerControlInterface& localComputerControlInterface,
										   QWidget* parent )
{
	Q_UNUSED(parent);

	if( feature == m_windowDemoFeature || feature == m_fullscreenDemoFeature )
	{
		sendFeatureMessage( FeatureMessage( m_demoClientFeature.uid(), StopDemoClient ), computerControlInterfaces );

		for( auto computerControlInterface : computerControlInterfaces )
		{
			m_demoClientHosts.removeAll( computerControlInterface->computer().hostAddress() );
		}

		qDebug() << "DemoFeaturePlugin::stopMasterFeature(): clients:" << m_demoClientHosts;

		// no demo clients left?
		if( m_demoClientHosts.isEmpty() )
		{
			// then we can stop the server
			localComputerControlInterface.sendFeatureMessage( FeatureMessage( m_demoServerFeature.uid(), StopDemoServer ) );
		}

		return true;
	}

	return false;
}



bool DemoFeaturePlugin::handleMasterFeatureMessage( const FeatureMessage& message,
													ComputerControlInterface& computerControlInterface )
{
	Q_UNUSED(message);
	Q_UNUSED(computerControlInterface);

	return false;
}



bool DemoFeaturePlugin::handleServiceFeatureMessage( const FeatureMessage& message,
													 FeatureWorkerManager& featureWorkerManager )
{
	if( message.featureUid() == m_demoServerFeature.uid() )
	{
		if( featureWorkerManager.isWorkerRunning( m_demoServerFeature ) == false )
		{
			featureWorkerManager.startWorker( m_demoServerFeature );
		}

		if( VeyonCore::authenticationCredentials().hasCredentials( AuthenticationCredentials::Token ) == false )
		{
			VeyonCore::authenticationCredentials().setToken( message.argument( VncServerToken ).toString() );
		}

		// forward message to worker
		featureWorkerManager.sendMessage( message );

		return true;
	}
	else if( message.featureUid() == m_demoClientFeature.uid() )
	{
		// if a demo server is started, it's likely that the demo accidentally was
		// started on master computer as well therefore we deny starting a demo on
		// hosts on which a demo server is running - exception: debug mode
		if( featureWorkerManager.isWorkerRunning( m_demoServerFeature ) &&
				VeyonCore::config().logLevel() < Logger::LogLevelDebug )
		{
			return false;
		}

		if( featureWorkerManager.isWorkerRunning( m_demoClientFeature ) == false )
		{
			featureWorkerManager.startWorker( m_demoClientFeature );
		}

		QTcpSocket* socket = dynamic_cast<QTcpSocket *>( message.ioDevice() );
		if( socket == nullptr )
		{
			qCritical( "DemoFeaturePlugin::handleServiceFeatureMessage(): socket is NULL!" );
			return false;
		}

		if( message.command() == StartDemoClient )
		{
			// construct a new message as we have to append the peer address as demo server host
			FeatureMessage startDemoClientMessage( message.featureUid(), message.command() );
			startDemoClientMessage.addArgument( DemoAccessToken, message.argument( DemoAccessToken ) );
			startDemoClientMessage.addArgument( IsFullscreenDemo, message.argument( IsFullscreenDemo ) );
			startDemoClientMessage.addArgument( DemoServerHost, socket->peerAddress().toString() );
			featureWorkerManager.sendMessage( startDemoClientMessage );
		}
		else
		{
			// forward message to worker
			featureWorkerManager.sendMessage( message );
		}

		return true;
	}

	return false;
}



bool DemoFeaturePlugin::handleWorkerFeatureMessage( const FeatureMessage& message )
{
	if( message.featureUid() == m_demoServerFeature.uid() )
	{
		switch( message.command() )
		{
		case StartDemoServer:
			if( m_demoServer == nullptr )
			{
				m_demoServer = new DemoServer( message.argument( VncServerToken ).toString(),
											   message.argument( DemoAccessToken ).toString(),
											   this );
			}
			return true;

		case StopDemoServer:
			delete m_demoServer;
			m_demoServer = nullptr;
			return true;

		default:
			break;
		}
	}
	else if( message.featureUid() == m_demoClientFeature.uid() )
	{
		switch( message.command() )
		{
		case StartDemoClient:
			VeyonCore::authenticationCredentials().setToken( message.argument( DemoAccessToken ).toString() );

			if( m_demoClient == nullptr )
			{
				qDebug() << "DemoClient: connecting with master" << message.argument( DemoServerHost ).toString();
				m_demoClient = new DemoClient( message.argument( DemoServerHost ).toString(),
											   message.argument( IsFullscreenDemo ).toBool() );
			}
			return true;

		case StopDemoClient:
			delete m_demoClient;
			m_demoClient = nullptr;
			return true;

		default:
			break;
		}
	}

	return false;
}
